#include "afterimage/core/DocumentManager.h"
#include "afterimage/extraction/ExtractionOptions.h"
#include "afterimage/extraction/ExtractionService.h"
#include "afterimage/indexing/InvertedIndex.h"
#include "afterimage/graph/MemoryGraph.h"
#include "afterimage/ranking/RankingEngine.h"
#include "afterimage/ranking/RankingOptions.h"
#include "afterimage/retrieval/FragmentRetrievalEngine.h"
#include "afterimage/retrieval/RetrievalOptions.h"
#include "afterimage/storage/SQLiteDocumentStore.h"
#include "afterimage/ingestion/IngestionService.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

enum class Mode { InteractiveSearch, SearchOnce, Index, Stats };

struct CliOptions {
    Mode mode{Mode::InteractiveSearch};
    std::filesystem::path folder;
    std::filesystem::path database;
    std::optional<std::string> query;
    bool forceReextract{false};
    bool phraseOnly{false};
    bool requireAllTerms{false};
    bool exactOnly{false};
    bool disablePrefix{false};
    bool disableInfix{false};
    bool disableFuzzy{false};
    bool disableRecency{false};
    bool jsonOutput{false};
    std::size_t maxResults{10};
    afterimage::ExtractionOptions extraction;
    bool showHelp{false};
};

std::filesystem::path defaultDatabasePath() {
    if (const char* overridePath = std::getenv("AFTERIMAGE_DB");
        overridePath != nullptr && *overridePath != '\0') {
        return overridePath;
    }
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "AFTERIMAGE" / "library.db";
    }
#else
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        const auto preferred = std::filesystem::path(home) / ".afterimage" / "library.db";
        // Preserve libraries created by Module 7 when the app was launched
        // from its project root with the old relative data/afterimage.db path.
        const auto legacy = std::filesystem::current_path() / "data" / "afterimage.db";
        std::error_code error;
        if (!std::filesystem::exists(preferred, error) && !error &&
            std::filesystem::exists(legacy, error) && !error) {
            return legacy;
        }
        return preferred;
    }
#endif
    return std::filesystem::current_path() / ".afterimage" / "library.db";
}

std::size_t parsePositiveInteger(const std::string& value, const std::string& option,
                                 std::size_t maximum) {
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size() || parsed == 0ULL || parsed > maximum) {
            throw std::invalid_argument("out of range");
        }
        return static_cast<std::size_t>(parsed);
    } catch (...) {
        throw std::invalid_argument(option + " must be between 1 and " + std::to_string(maximum) + ".");
    }
}

void printHelp() {
    std::cout << R"HELP(AFTERIMAGE — search your local documents by fragments of memory

USAGE
  afterimage                         Open the interactive search prompt
  afterimage index <folder>          Index a folder (including subfolders)
  afterimage search "some fragment"  Run one search and exit

EXAMPLES
  afterimage index ~/Documents
  afterimage
  afterimage search "raft leader heartbeat"
  afterimage search "schedulng" --max-results 5
  afterimage index ~/Scans --force

OPTIONS
  --db <path>          Use a specific SQLite library database
  --force              Re-extract every known document in the library
  --no-ocr             Disable OCR for images and scanned PDFs
  --ocr-language <id>  Tesseract language, for example eng or hin+eng
  --max-results <N>    Maximum results to show (1–100)
  --phrase             Require an exact consecutive phrase
  --all-terms          Require every query word/fragment to match
  --exact-only         Disable prefix, infix, fuzzy, and phrase expansion
  --no-prefix          Disable prefix expansion
  --no-infix           Disable middle-of-word expansion
  --no-fuzzy           Disable typo-tolerant fallback
  --no-recency         Rank by content only, ignoring file modification time
  -h, --help           Show this help

INTERACTIVE COMMANDS
  :help                Show interactive commands
  :stats               Show library, index, and graph statistics
  :quit                Exit AFTERIMAGE

SEARCH RESULTS
  Direct matches are ranked first. Related memories are shown separately
  when the relationship graph finds useful shared concepts.

DEFAULT DATABASE
  ~/.afterimage/library.db  (override with --db or AFTERIMAGE_DB)
)HELP";
}

CliOptions parseArguments(int argc, char* argv[]) {
    CliOptions options;
    options.database = defaultDatabasePath();
    int index = 1;

    if (index < argc) {
        const std::string first = argv[index];
        if (first == "--help" || first == "-h") return options;
        if (first == "index") {
            options.mode = Mode::Index;
            ++index;
            if (index >= argc) throw std::invalid_argument("index requires a folder path.");
            options.folder = argv[index++];
        } else if (first == "search") {
            options.mode = Mode::SearchOnce;
            ++index;
            if (index >= argc) throw std::invalid_argument("search requires a quoted query fragment.");
            options.query = argv[index++];
        } else if (first == "stats") {
            options.mode = Mode::Stats;
            ++index;
        } else if (first == "--search" || first == "-q") {
            options.mode = Mode::SearchOnce;
            ++index;
            if (index >= argc) throw std::invalid_argument("--search requires a quoted query fragment.");
            options.query = argv[index++];
        } else if (!first.starts_with("--")) {
            // Backward-compatible convenience: `afterimage <folder>` means index it.
            options.mode = Mode::Index;
            options.folder = first;
            ++index;
        }
    }

    while (index < argc) {
        const std::string value = argv[index++];
        auto requireValue = [&](const std::string& option) -> std::string {
            if (index >= argc) throw std::invalid_argument(option + " requires a value.");
            return argv[index++];
        };

        if (value == "--db") options.database = requireValue(value);
        else if (value == "--json") options.jsonOutput = true;
        else if (value == "--force" || value == "--reextract") options.forceReextract = true;
        else if (value == "--no-ocr") options.extraction.enableOcr = false;
        else if (value == "--ocr-language") options.extraction.ocrLanguage = requireValue(value);
        else if (value == "--max-results") options.maxResults = parsePositiveInteger(requireValue(value), value, 100);
        else if (value == "--phrase") options.phraseOnly = true;
        else if (value == "--all-terms") options.requireAllTerms = true;
        else if (value == "--exact-only") options.exactOnly = true;
        else if (value == "--no-prefix") options.disablePrefix = true;
        else if (value == "--no-infix") options.disableInfix = true;
        else if (value == "--no-fuzzy") options.disableFuzzy = true;
        else if (value == "--no-recency") options.disableRecency = true;
        else if (value == "--help" || value == "-h") options.showHelp = true;
        else throw std::invalid_argument("Unknown option: " + value);
    }
    return options;
}

void restoreLibrary(afterimage::SQLiteDocumentStore& store, afterimage::DocumentManager& library) {
    for (auto& document : store.loadAll()) library.restoreDocument(std::move(document));
}

afterimage::InvertedIndex buildIndex(const afterimage::DocumentManager& library) {
    afterimage::InvertedIndex index;
    for (const auto& document : library.allDocuments()) index.addDocument(document);
    return index;
}

std::string escapeJson(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

void printSearchResultsJson(
    const std::string& query,
    const std::vector<afterimage::RankedResult>& results,
    const std::vector<afterimage::RelatedDocument>& related,
    const afterimage::DocumentManager& library
) {
    std::cout << "{\n"
              << "  \"query\": \"" << escapeJson(query) << "\",\n"
              << "  \"count\": " << results.size() << ",\n"
              << "  \"results\": [\n";

    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& ranked = results[i];
        const auto document = library.findById(ranked.retrieval.documentId);
        if (!document) continue;

        std::string preview;
        if (!document->extractedText.empty()) {
            preview = document->extractedText.substr(0, 320);
            std::replace(preview.begin(), preview.end(), '\n', ' ');
            std::replace(preview.begin(), preview.end(), '\r', ' ');
            if (document->extractedText.size() > preview.size()) preview += "...";
        }

        std::cout << "    {\n"
                  << "      \"rank\": " << (i + 1) << ",\n"
                  << "      \"id\": " << document->id << ",\n"
                  << "      \"title\": \"" << escapeJson(document->title) << "\",\n"
                  << "      \"filePath\": \"" << escapeJson(document->filePath) << "\",\n"
                  << "      \"fileType\": \"" << escapeJson(document->fileType) << "\",\n"
                  << "      \"sizeBytes\": " << (document->sizeBytes ? std::to_string(*document->sizeBytes) : "null") << ",\n"
                  << "      \"modifiedAtEpochSeconds\": " << (document->modifiedAtEpochSeconds ? std::to_string(*document->modifiedAtEpochSeconds) : "null") << ",\n"
                  << "      \"score\": " << std::fixed << std::setprecision(4) << ranked.breakdown.finalScore << ",\n"
                  << "      \"matchedBy\": \"" << escapeJson(ranked.retrieval.explanation()) << "\",\n"
                  << "      \"breakdown\": {\n"
                  << "        \"bm25Score\": " << ranked.breakdown.bm25Score << ",\n"
                  << "        \"titleBoost\": " << ranked.breakdown.titleBoost << ",\n"
                  << "        \"pathBoost\": " << ranked.breakdown.pathBoost << ",\n"
                  << "        \"phraseBoost\": " << ranked.breakdown.phraseBoost << ",\n"
                  << "        \"coverageBoost\": " << ranked.breakdown.coverageBoost << ",\n"
                  << "        \"recencyBoost\": " << ranked.breakdown.recencyBoost << "\n"
                  << "      },\n"
                  << "      \"preview\": \"" << escapeJson(preview) << "\"\n"
                  << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
    }

    std::cout << "  ],\n"
              << "  \"related\": [\n";

    std::unordered_set<afterimage::DocumentId> directIds;
    for (const auto& item : results) directIds.insert(item.retrieval.documentId);

    bool firstRelated = true;
    for (const auto& item : related) {
        if (directIds.contains(item.documentId)) continue;
        const auto document = library.findById(item.documentId);
        if (!document) continue;

        if (!firstRelated) std::cout << ",\n";
        firstRelated = false;

        std::cout << "    {\n"
                  << "      \"id\": " << document->id << ",\n"
                  << "      \"title\": \"" << escapeJson(document->title) << "\",\n"
                  << "      \"filePath\": \"" << escapeJson(document->filePath) << "\",\n"
                  << "      \"score\": " << std::fixed << std::setprecision(4) << item.score << ",\n"
                  << "      \"connectingConcepts\": [";
        for (std::size_t c = 0; c < item.connectingConcepts.size(); ++c) {
            std::cout << "\"" << escapeJson(item.connectingConcepts[c]) << "\""
                      << (c + 1 < item.connectingConcepts.size() ? ", " : "");
        }
        std::cout << "]\n    }";
    }

    std::cout << "\n  ]\n}\n";
}

void printSearchResults(
    const std::string& query,
    const afterimage::RetrievalOptions& retrievalOptions,
    const afterimage::RankingOptions& rankingOptions,
    const afterimage::FragmentRetrievalEngine& retrieval,
    const afterimage::RankingEngine& ranking,
    const afterimage::InvertedIndex& index,
    const afterimage::DocumentManager& library,
    const afterimage::MemoryGraph& graph
) {
    if (query.find_first_not_of(" \t\r\n") == std::string::npos) {
        std::cout << "Please enter a word or a fragment to search.\n";
        return;
    }

    const auto candidates = retrieval.retrieve(query, index, retrievalOptions);
    const auto results = ranking.rank(query, candidates, index, library.allDocuments(), rankingOptions);
    std::cout << "\nMatches for: \"" << query << "\"\n";
    if (results.empty()) {
        std::cout << "  No matches yet. Try fewer words or a shorter fragment.\n";
        return;
    }

    std::vector<afterimage::DocumentId> seedIds;
    for (std::size_t position = 0; position < results.size(); ++position) {
        const auto& ranked = results[position];
        const auto document = library.findById(ranked.retrieval.documentId);
        if (!document) continue;
        if (seedIds.size() < 3) seedIds.push_back(document->id);

        std::cout << "\n" << position + 1 << ". " << document->title
                  << "  [score " << std::fixed << std::setprecision(2)
                  << ranked.breakdown.finalScore << "]\n"
                  << "   " << document->filePath << "\n"
                  << "   Matched by: " << ranked.retrieval.explanation() << "\n";

        if (!document->extractedText.empty()) {
            std::string preview = document->extractedText.substr(0, 240);
            std::replace(preview.begin(), preview.end(), '\n', ' ');
            std::replace(preview.begin(), preview.end(), '\r', ' ');
            std::cout << "   " << preview;
            if (document->extractedText.size() > preview.size()) std::cout << "...";
            std::cout << '\n';
        }
    }

    // Graph expansion is supplementary: it never displaces direct search matches.
    afterimage::GraphOptions graphOptions;
    graphOptions.maximumResults = 5;
    const auto related = graph.relatedDocuments(seedIds, graphOptions);
    if (!related.empty()) {
        std::unordered_set<afterimage::DocumentId> directIds;
        for (const auto& item : results) directIds.insert(item.retrieval.documentId);
        std::cout << "\nRelated memories (connected by shared concepts):\n";
        std::size_t shown = 0;
        for (const auto& item : related) {
            if (directIds.contains(item.documentId)) continue;
            const auto document = library.findById(item.documentId);
            if (!document) continue;
            std::cout << "  - " << document->title << " [graph score "
                      << std::fixed << std::setprecision(2) << item.score << "]\n"
                      << "    Connected through: ";
            for (std::size_t i = 0; i < item.connectingConcepts.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << item.connectingConcepts[i];
            }
            std::cout << "\n    " << document->filePath << "\n";
            if (++shown >= 5) break;
        }
    }
}

void runStats(const CliOptions& options) {
    afterimage::SQLiteDocumentStore store(options.database);
    afterimage::DocumentManager library;
    restoreLibrary(store, library);
    const afterimage::InvertedIndex index = buildIndex(library);
    afterimage::MemoryGraph graph;
    graph.build(library.allDocuments());

    if (options.jsonOutput) {
        std::cout << "{\n"
                  << "  \"documents\": " << library.size() << ",\n"
                  << "  \"vocabulary\": " << index.vocabularySize() << ",\n"
                  << "  \"postings\": " << index.postingCount() << ",\n"
                  << "  \"concepts\": " << graph.conceptCount() << ",\n"
                  << "  \"edges\": " << graph.edgeCount() << ",\n"
                  << "  \"database\": \"" << escapeJson(options.database.string()) << "\"\n"
                  << "}\n";
    } else {
        std::cout << "Documents: " << library.size() << "\n"
                  << "Vocabulary: " << index.vocabularySize() << " unique terms\n"
                  << "Postings: " << index.postingCount() << " term-document links\n"
                  << "Graph concepts: " << graph.conceptCount() << "\n"
                  << "Graph edges: " << graph.edgeCount() << "\n";
    }
}

void runSearch(const CliOptions& options) {
    afterimage::SQLiteDocumentStore store(options.database);
    afterimage::DocumentManager library;
    restoreLibrary(store, library);

    if (options.jsonOutput) {
        if (library.empty()) {
            std::cout << "{\n  \"query\": \"" << escapeJson(options.query.value_or("")) << "\",\n  \"count\": 0,\n  \"results\": [],\n  \"related\": []\n}\n";
            return;
        }
        const afterimage::InvertedIndex index = buildIndex(library);
        afterimage::MemoryGraph graph;
        graph.build(library.allDocuments());
        afterimage::RetrievalOptions retrievalOptions;
        retrievalOptions.phraseOnly = options.phraseOnly;
        retrievalOptions.requireAllFragments = options.requireAllTerms;
        retrievalOptions.maxResults = 1000;
        if (options.exactOnly) {
            retrievalOptions.enablePrefix = false;
            retrievalOptions.enableInfix = false;
            retrievalOptions.enableFuzzy = false;
            retrievalOptions.enablePhrase = false;
        }
        if (options.disablePrefix) retrievalOptions.enablePrefix = false;
        if (options.disableInfix) retrievalOptions.enableInfix = false;
        if (options.disableFuzzy) retrievalOptions.enableFuzzy = false;

        afterimage::RankingOptions rankingOptions;
        rankingOptions.maxResults = options.maxResults;
        rankingOptions.enableRecencyBoost = !options.disableRecency;

        const afterimage::FragmentRetrievalEngine retrieval;
        const afterimage::RankingEngine ranking;

        const auto candidates = retrieval.retrieve(options.query.value_or(""), index, retrievalOptions);
        const auto results = ranking.rank(options.query.value_or(""), candidates, index, library.allDocuments(), rankingOptions);
        std::vector<afterimage::DocumentId> seedIds;
        for (std::size_t i = 0; i < std::min<std::size_t>(3, results.size()); ++i) {
            seedIds.push_back(results[i].retrieval.documentId);
        }
        afterimage::GraphOptions graphOptions;
        graphOptions.maximumResults = 5;
        const auto related = graph.relatedDocuments(seedIds, graphOptions);
        printSearchResultsJson(options.query.value_or(""), results, related, library);
        return;
    }

    if (library.empty()) {
        std::cout << "Your AFTERIMAGE library is empty.\n\n"
                  << "Index a folder first:\n"
                  << "  afterimage index ~/Documents\n\n"
                  << "Database: " << options.database.string() << '\n';
        return;
    }

    const afterimage::InvertedIndex index = buildIndex(library);
    afterimage::MemoryGraph graph;
    graph.build(library.allDocuments());
    afterimage::RetrievalOptions retrievalOptions;
    retrievalOptions.phraseOnly = options.phraseOnly;
    retrievalOptions.requireAllFragments = options.requireAllTerms;
    retrievalOptions.maxResults = 1000;
    if (options.exactOnly) {
        retrievalOptions.enablePrefix = false;
        retrievalOptions.enableInfix = false;
        retrievalOptions.enableFuzzy = false;
        retrievalOptions.enablePhrase = false;
    }
    if (options.disablePrefix) retrievalOptions.enablePrefix = false;
    if (options.disableInfix) retrievalOptions.enableInfix = false;
    if (options.disableFuzzy) retrievalOptions.enableFuzzy = false;

    afterimage::RankingOptions rankingOptions;
    rankingOptions.maxResults = options.maxResults;
    rankingOptions.enableRecencyBoost = !options.disableRecency;

    const afterimage::FragmentRetrievalEngine retrieval;
    const afterimage::RankingEngine ranking;

    std::cout << "AFTERIMAGE library: " << library.size() << " documents | "
              << index.vocabularySize() << " searchable terms\n";
    std::cout << "Database: " << options.database.string() << '\n';

    if (options.mode == Mode::SearchOnce && options.query) {
        printSearchResults(*options.query, retrievalOptions, rankingOptions,
                           retrieval, ranking, index, library, graph);
        return;
    }

    std::cout << "\nSearch any word, phrase, or fragment. Try :help or :quit.\n";
    std::string query;
    while (true) {
        std::cout << "\nafterimage> " << std::flush;
        if (!std::getline(std::cin, query)) break;
        if (query == ":quit" || query == ":q" || query == ":exit") break;
        if (query == ":help") {
            std::cout << "Enter a remembered word or fragment. Examples: 'raft leader', 'sched', 'ntersection'.\n"
                         "Direct matches come first; related memories appear below when shared concepts connect them.\n"
                         ":stats shows library, index and graph statistics. :quit exits.\n";
            continue;
        }
        if (query == ":stats") {
            std::cout << "Documents: " << library.size() << "\n"
                      << "Vocabulary: " << index.vocabularySize() << " unique terms\n"
                      << "Postings: " << index.postingCount() << " term-document links\n"
                      << "Graph concepts: " << graph.conceptCount() << "\n"
                      << "Graph edges: " << graph.edgeCount() << "\n";
            continue;
        }
        if (query.empty()) continue;
        printSearchResults(query, retrievalOptions, rankingOptions,
                           retrieval, ranking, index, library, graph);
    }
}

void runIndex(const CliOptions& options) {
    afterimage::SQLiteDocumentStore store(options.database);
    afterimage::DocumentManager library;
    restoreLibrary(store, library);

    std::cout << "AFTERIMAGE — indexing local documents\n"
              << "Folder: " << options.folder.string() << '\n'
              << "Database: " << options.database.string() << '\n';

    afterimage::IngestionService ingestion;
    const auto ingestReport = ingestion.ingestFolder(options.folder, library);
    if (!ingestReport.scan.rootValid) {
        std::cerr << "Could not scan the selected folder.\n";
        for (const auto& issue : ingestReport.scan.issues) {
            std::cerr << "  " << issue.path.string() << ": " << issue.message << '\n';
        }
        throw std::runtime_error("Indexing stopped because the folder could not be read.");
    }

    std::vector<afterimage::DocumentId> idsToExtract;
    if (options.forceReextract) {
        idsToExtract.reserve(library.size());
        for (const auto& document : library.allDocuments()) idsToExtract.push_back(document.id);
    } else {
        idsToExtract = ingestReport.importedDocumentIds;
        idsToExtract.insert(idsToExtract.end(), ingestReport.updatedDocumentIds.begin(),
                            ingestReport.updatedDocumentIds.end());
    }

    afterimage::ExtractionService extraction(options.extraction);
    const auto extractionReport = extraction.extractDocuments(library, idsToExtract);
    store.saveAll(library.allDocuments());
    const afterimage::InvertedIndex index = buildIndex(library);

    std::cout << "\nIndexing complete\n"
              << "  New files:             " << ingestReport.importedDocumentIds.size() << '\n'
              << "  Changed files:         " << ingestReport.changedFilesDetected << '\n'
              << "  Unchanged known files: " << ingestReport.skippedAlreadyImported - ingestReport.changedFilesDetected << '\n'
              << "  Files extracted:       " << extractionReport.extracted << '\n'
              << "  Partial extraction:    " << extractionReport.partial << '\n'
              << "  Empty documents:       " << extractionReport.empty << '\n'
              << "  Missing dependencies:  " << extractionReport.dependencyMissing << '\n'
              << "  Extraction failures:   " << extractionReport.failed << '\n'
              << "  Too large:             " << extractionReport.tooLarge << '\n'
              << "  Library documents:     " << library.size() << '\n'
              << "  Searchable terms:      " << index.vocabularySize() << '\n'
              << "\nYour library is saved. Search it with:\n"
              << "  afterimage\n"
              << "  afterimage search \"a fragment you remember\"\n";

    for (const auto& message : extractionReport.messages) {
        if (message.find("Text extracted") == std::string::npos &&
            message.find("Image processed") == std::string::npos &&
            message.find("PDF text extracted") == std::string::npos &&
            message.find("Scanned PDF processed") == std::string::npos) {
            std::cout << "  Note: " << message << '\n';
        }
    }
    for (const auto& issue : ingestReport.scan.issues) {
        std::cerr << "Warning: " << issue.path.string() << ": " << issue.message << '\n';
    }
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const CliOptions options = parseArguments(argc, argv);
        if (options.showHelp || (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))) {
            printHelp();
            return 0;
        }
        if (options.mode == Mode::Index) runIndex(options);
        else if (options.mode == Mode::Stats) runStats(options);
        else runSearch(options);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Argument error: " << error.what() << "\n\n";
        printHelp();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "AFTERIMAGE error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
