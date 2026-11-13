file(REMOVE "${AFTERIMAGE_DB}" "${AFTERIMAGE_DB}-wal" "${AFTERIMAGE_DB}-shm")

execute_process(
    COMMAND "${AFTERIMAGE_BIN}" index "${AFTERIMAGE_CORPUS}" --db "${AFTERIMAGE_DB}" --no-ocr
    RESULT_VARIABLE index_result
    OUTPUT_VARIABLE index_output
    ERROR_VARIABLE index_error
    TIMEOUT 90
)
if(NOT index_result EQUAL 0)
    message(FATAL_ERROR "AFTERIMAGE index command failed: ${index_error}\n${index_output}")
endif()
if(NOT index_output MATCHES "Library documents:[ ]+16")
    message(FATAL_ERROR "Index command did not import all 16 corpus documents:\n${index_output}")
endif()

execute_process(
    COMMAND "${AFTERIMAGE_BIN}" search "raft leader heartbeat" --db "${AFTERIMAGE_DB}" --no-recency
    RESULT_VARIABLE search_result
    OUTPUT_VARIABLE search_output
    ERROR_VARIABLE search_error
    TIMEOUT 30
)
if(NOT search_result EQUAL 0)
    message(FATAL_ERROR "AFTERIMAGE search command failed: ${search_error}\n${search_output}")
endif()
if(NOT search_output MATCHES "raft_consensus")
    message(FATAL_ERROR "Search command did not return the expected Raft document:\n${search_output}")
endif()
if(NOT search_output MATCHES "Related memories")
    message(FATAL_ERROR "Search command did not include graph-based related memories:\n${search_output}")
endif()
if(NOT search_output MATCHES "distributed_transactions")
    message(FATAL_ERROR "Graph expansion did not surface the expected related distributed-systems document:\n${search_output}")
endif()

execute_process(
    COMMAND "${AFTERIMAGE_BIN}" index "${AFTERIMAGE_CORPUS}" --db "${AFTERIMAGE_DB}" --no-ocr
    RESULT_VARIABLE second_index_result
    OUTPUT_VARIABLE second_index_output
    ERROR_VARIABLE second_index_error
    TIMEOUT 60
)
if(NOT second_index_result EQUAL 0)
    message(FATAL_ERROR "Second index command failed: ${second_index_error}\n${second_index_output}")
endif()
if(NOT second_index_output MATCHES "Files extracted:[ ]+0")
    message(FATAL_ERROR "Idempotent second index unexpectedly re-extracted unchanged files:\n${second_index_output}")
endif()

message(STATUS "CLI workflow passed: index → persisted library → search → idempotent re-index.")
