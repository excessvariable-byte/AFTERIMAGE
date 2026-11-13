#include "afterimage/retrieval/FragmentRetrievalEngine.h"
#include "afterimage/indexing/Tokeniser.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace afterimage {

const char* retrievalPathName(RetrievalPath path) noexcept {
    switch (path) {
        case RetrievalPath::Exact: return "exact";
        case RetrievalPath::Prefix: return "prefix";
        case RetrievalPath::Infix: return "infix";
        case RetrievalPath::Fuzzy: return "fuzzy";
        case RetrievalPath::Phrase: return "phrase";
    }
    return "unknown";
}

std::string RetrievalResult::explanation() const {
    if (matches.empty()) {
        return "No retrieval evidence available.";
    }

    std::string output;
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& match = matches[i];
        if (i > 0) {
            output += "; ";
        }
        if (match.path == RetrievalPath::Phrase) {
            output += "exact phrase '" + match.queryFragment + "' matched";
        } else {
            output += std::string(retrievalPathName(match.path)) + " fragment '" +
                match.queryFragment + "' matched '" + match.matchedTerm + "'";
        }
    }
    return output;
}

std::vector<RetrievalResult> FragmentRetrievalEngine::retrieve(
    std::string_view query,
    const InvertedIndex& index,
    const RetrievalOptions& options) const {
    const auto queryTokens = Tokeniser::tokenize(query);
    if (queryTokens.empty() || index.documentCount() == 0 || options.maxResults == 0) {
        return {};
    }

    std::vector<std::string> fragments;
    std::unordered_set<std::string> uniqueFragments;
    for (const auto& token : queryTokens) {
        if (uniqueFragments.insert(token.text).second) {
            fragments.push_back(token.text);
        }
    }

    std::unordered_map<DocumentId, RetrievalResult> candidates;
    std::unordered_map<DocumentId, std::unordered_set<std::string>> matchedByDocument;
    std::unordered_map<DocumentId, std::unordered_set<std::string>> evidenceKeys;

    const auto addEvidence = [&](const SearchResult& searchResult,
                                 const std::string& queryFragment,
                                 const std::string& matchedTerm,
                                 RetrievalPath path,
                                 double weight) {
        if (searchResult.score <= 0.0 || weight <= 0.0) {
            return;
        }

        const std::string key = std::to_string(static_cast<int>(path)) + "|" +
            queryFragment + "|" + matchedTerm;
        auto& seen = evidenceKeys[searchResult.documentId];
        if (!seen.insert(key).second) {
            return;
        }

        auto& result = candidates[searchResult.documentId];
        result.documentId = searchResult.documentId;
        const double contribution = searchResult.score * weight;
        result.score += contribution;
        result.matches.push_back(RetrievalMatch{
            queryFragment, matchedTerm, path, contribution
        });
        if (path == RetrievalPath::Phrase) {
            result.phraseMatched = true;
        } else {
            matchedByDocument[searchResult.documentId].insert(queryFragment);
        }
    };

    const auto addTerm = [&](const std::string& fragment,
                             const std::string& matchedTerm,
                             RetrievalPath path,
                             double weight) {
        SearchOptions exactOptions;
        exactOptions.prefixMatch = false;
        const auto results = index.search(matchedTerm, exactOptions);
        for (const auto& result : results) {
            addEvidence(result, fragment, matchedTerm, path, weight);
        }
        return !results.empty();
    };

    if (options.phraseOnly) {
        if (fragments.size() < 2) {
            return {};
        }
        SearchOptions phraseOptions;
        phraseOptions.phraseMatch = true;
        const auto phraseResults = index.search(query, phraseOptions);
        for (const auto& result : phraseResults) {
            addEvidence(result, std::string(query), std::string(query),
                        RetrievalPath::Phrase, options.phraseWeight);
        }
    } else {
        for (const auto& fragment : fragments) {
            // Fuzzy matching is a fallback, not an additional source of noise:
            // use it only when exact, prefix, and infix routes found no candidates.
            bool hasStrongMatch = false;
            if (options.enableExact) {
                hasStrongMatch = addTerm(fragment, fragment, RetrievalPath::Exact,
                                         options.exactWeight) || hasStrongMatch;
            }

            if (options.enablePrefix && options.maxPrefixExpansions > 0) {
                const auto expansions = index.expandPrefix(fragment, options.maxPrefixExpansions);
                for (const auto& term : expansions) {
                    if (term != fragment) {
                        hasStrongMatch = addTerm(fragment, term, RetrievalPath::Prefix,
                                                 options.prefixWeight) || hasStrongMatch;
                    }
                }
            }

            if (options.enableInfix && fragment.size() >= options.minimumInfixLength &&
                options.maxInfixExpansions > 0) {
                const auto expansions = index.expandContaining(fragment, options.maxInfixExpansions + 1);
                for (const auto& term : expansions) {
                    if (term != fragment && !term.starts_with(fragment)) {
                        hasStrongMatch = addTerm(fragment, term, RetrievalPath::Infix,
                                                 options.infixWeight) || hasStrongMatch;
                    }
                }
            }

            if (options.enableFuzzy && !hasStrongMatch &&
                fragment.size() >= options.minimumFuzzyLength &&
                options.maxFuzzyExpansions > 0 && options.maxEditDistance > 0) {
                const auto expansions = index.expandFuzzy(
                    fragment, options.maxEditDistance, options.maxFuzzyExpansions + 1);
                for (const auto& term : expansions) {
                    // Exact, prefix and infix matches already have stronger evidence.
                    if (term == fragment || term.starts_with(fragment) ||
                        term.find(fragment) != std::string::npos) {
                        continue;
                    }
                    addTerm(fragment, term, RetrievalPath::Fuzzy, options.fuzzyWeight);
                }
            }
        }

        if (options.enablePhrase && fragments.size() > 1) {
            SearchOptions phraseOptions;
            phraseOptions.phraseMatch = true;
            const auto phraseResults = index.search(query, phraseOptions);
            for (const auto& result : phraseResults) {
                addEvidence(result, std::string(query), std::string(query),
                            RetrievalPath::Phrase, options.phraseWeight);
            }
        }
    }

    std::vector<RetrievalResult> results;
    results.reserve(candidates.size());
    for (auto& [id, result] : candidates) {
        result.matchedFragments = matchedByDocument[id].size();
        if (options.phraseOnly && result.phraseMatched) {
            result.matchedFragments = fragments.size();
        }
        if (options.requireAllFragments && !options.phraseOnly &&
            result.matchedFragments != fragments.size()) {
            continue;
        }
        std::sort(result.matches.begin(), result.matches.end(),
            [](const RetrievalMatch& left, const RetrievalMatch& right) {
                if (left.contribution != right.contribution) {
                    return left.contribution > right.contribution;
                }
                if (left.path != right.path) {
                    return static_cast<int>(left.path) < static_cast<int>(right.path);
                }
                return left.matchedTerm < right.matchedTerm;
            });
        results.push_back(std::move(result));
    }

    std::sort(results.begin(), results.end(),
        [](const RetrievalResult& left, const RetrievalResult& right) {
            if (std::abs(left.score - right.score) > 1e-12) {
                return left.score > right.score;
            }
            return left.documentId < right.documentId;
        });

    if (results.size() > options.maxResults) {
        results.resize(options.maxResults);
    }
    return results;
}

} // namespace afterimage
