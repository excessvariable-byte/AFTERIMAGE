file(REMOVE "${AFTERIMAGE_DB}" "${AFTERIMAGE_DB}-wal" "${AFTERIMAGE_DB}-shm")

execute_process(
    COMMAND "${AFTERIMAGE_BIN}" index "${AFTERIMAGE_FIXTURES}" --db "${AFTERIMAGE_DB}"
    RESULT_VARIABLE index_result
    OUTPUT_VARIABLE index_output
    ERROR_VARIABLE index_error
    TIMEOUT 120
)
if(NOT index_result EQUAL 0)
    message(FATAL_ERROR "Media CLI indexing failed: ${index_error}\n${index_output}")
endif()
if(NOT index_output MATCHES "Library documents:[ ]+4")
    message(FATAL_ERROR "Media CLI did not index the four PDF/image fixtures:\n${index_output}")
endif()

# The tools are optional for builds, so report a visible skip when this
# machine does not have the media dependencies. The unit tests still verify
# that missing programs produce DependencyMissing, not a crash.
if(NOT index_output MATCHES "Missing dependencies:[ ]+0")
    message(STATUS "Media CLI OCR verification skipped because Poppler/Tesseract are not both available.")
    return()
endif()

execute_process(
    COMMAND "${AFTERIMAGE_BIN}" search "raft consensus leader heartbeat" --db "${AFTERIMAGE_DB}" --no-recency --max-results 10
    RESULT_VARIABLE search_result
    OUTPUT_VARIABLE search_output
    ERROR_VARIABLE search_error
    TIMEOUT 30
)
if(NOT search_result EQUAL 0)
    message(FATAL_ERROR "Media CLI search failed: ${search_error}\n${search_output}")
endif()
if(NOT search_output MATCHES "ocr_sample\\.png")
    message(FATAL_ERROR "Image OCR text was not searchable end-to-end:\n${search_output}")
endif()
if(NOT search_output MATCHES "scanned_pdf_fixture\\.pdf")
    message(FATAL_ERROR "Scanned-PDF OCR text was not searchable end-to-end:\n${search_output}")
endif()

message(STATUS "Media CLI workflow passed: PDF text, scanned PDF OCR, image OCR, SQLite persistence and search.")
