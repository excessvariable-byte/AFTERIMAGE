import CoreSpotlight
import Dispatch
import Foundation
import SQLite3

struct DocumentRecord {
    let id: Int64
    let title: String
    let path: String
    let type: String
    let tags: String
}

func text(_ statement: OpaquePointer?, _ column: Int32) -> String {
    guard let value = sqlite3_column_text(statement, column) else { return "" }
    return String(cString: value)
}

func loadDocuments(from databasePath: String) throws -> [DocumentRecord] {
    var database: OpaquePointer?
    guard sqlite3_open_v2(databasePath, &database, SQLITE_OPEN_READONLY, nil) == SQLITE_OK else {
        throw NSError(domain: "AfterimageSpotlight", code: 1,
                      userInfo: [NSLocalizedDescriptionKey: "Cannot open \(databasePath)"])
    }
    defer { sqlite3_close(database) }

    let query = "SELECT id, title, file_path, file_type, COALESCE(tags, '') FROM documents"
    var statement: OpaquePointer?
    guard sqlite3_prepare_v2(database, query, -1, &statement, nil) == SQLITE_OK else {
        throw NSError(domain: "AfterimageSpotlight", code: 2,
                      userInfo: [NSLocalizedDescriptionKey: "Cannot read AFTERIMAGE documents"])
    }
    defer { sqlite3_finalize(statement) }

    var documents: [DocumentRecord] = []
    while sqlite3_step(statement) == SQLITE_ROW {
        documents.append(DocumentRecord(
            id: sqlite3_column_int64(statement, 0), title: text(statement, 1),
            path: text(statement, 2), type: text(statement, 3), tags: text(statement, 4)
        ))
    }
    return documents
}

func spotlightItem(for document: DocumentRecord) -> CSSearchableItem {
    let attributes = CSSearchableItemAttributeSet(itemContentType: "public.data")
    attributes.title = document.title
    attributes.contentDescription = "AFTERIMAGE document · \(document.type.uppercased())"
    attributes.keywords = ([document.title, document.type] + document.tags.split(separator: ",").map(String.init))
    attributes.contentURL = URL(fileURLWithPath: document.path)
    return CSSearchableItem(
        uniqueIdentifier: "afterimage.document.\(document.id)",
        domainIdentifier: "afterimage.documents", attributeSet: attributes
    )
}

let arguments = CommandLine.arguments
guard arguments.count == 2 else {
    fputs("Usage: afterimage-spotlight /path/to/library.db\n", stderr)
    exit(64)
}

do {
    let documents = try loadDocuments(from: arguments[1])
    let index = CSSearchableIndex.default()
    let semaphore = DispatchSemaphore(value: 0)
    var indexingError: Error?
    index.indexSearchableItems(documents.map(spotlightItem)) { error in
        indexingError = error
        semaphore.signal()
    }
    semaphore.wait()
    if let indexingError {
        throw indexingError
    }
    print("Published \(documents.count) AFTERIMAGE documents to Spotlight.")
} catch {
    fputs("Spotlight sync failed: \(error.localizedDescription)\n", stderr)
    exit(1)
}
