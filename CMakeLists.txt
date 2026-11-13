cmake_minimum_required(VERSION 3.20)

project(AFTERIMAGE VERSION 1.0.0 LANGUAGES CXX)

option(AFTERIMAGE_BUILD_TESTS "Build AFTERIMAGE tests" ON)

find_package(SQLite3 REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(afterimage_core
    src/core/DocumentManager.cpp
    src/ingestion/FileScanner.cpp
    src/ingestion/IngestionService.cpp
    src/extraction/TextNormaliser.cpp
    src/extraction/ContentExtractor.cpp
    src/extraction/ExtractionService.cpp
    src/storage/SQLiteDocumentStore.cpp
    src/indexing/Tokeniser.cpp
    src/indexing/InvertedIndex.cpp
    src/retrieval/FragmentRetrievalEngine.cpp
    src/ranking/RankingEngine.cpp
    src/graph/MemoryGraph.cpp
)

target_link_libraries(afterimage_core PUBLIC SQLite::SQLite3)
target_include_directories(afterimage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
    target_compile_options(afterimage_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(afterimage src/main.cpp)
target_link_libraries(afterimage PRIVATE afterimage_core)

if(APPLE)
    add_custom_target(afterimage_spotlight ALL
        COMMAND xcrun swiftc ${CMAKE_CURRENT_SOURCE_DIR}/macos/AfterimageSpotlight.swift
                -framework CoreSpotlight -lsqlite3
                -o ${CMAKE_CURRENT_BINARY_DIR}/afterimage-spotlight
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/macos/AfterimageSpotlight.swift
        COMMENT "Building AFTERIMAGE Spotlight bridge"
    )

    add_custom_target(afterimage_macos_app ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/MacOS
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Resources
        COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/bin/env xcrun swiftc ${CMAKE_CURRENT_SOURCE_DIR}/macos/AfterimageLauncher.swift
                -target arm64-apple-macosx27.0 -framework AppKit
                -o ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/MacOS/Afterimage
        COMMAND /usr/bin/plutil -create xml1 ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundleExecutable string Afterimage" ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundleIdentifier string local.afterimage.launcher" ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundleInfoDictionaryVersion string 6.0" ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundleName string Afterimage" ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundlePackageType string APPL" ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string 1.0" ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundleVersion string 1" ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :LSMinimumSystemVersion string 13.0" ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        COMMAND /usr/libexec/PlistBuddy -c "Add :AfterimageRoot string ${CMAKE_CURRENT_SOURCE_DIR}" ${CMAKE_CURRENT_BINARY_DIR}/Afterimage.app/Contents/Info.plist
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/macos/AfterimageLauncher.swift
        COMMENT "Building AFTERIMAGE macOS launcher app"
    )
endif()

include(GNUInstallDirs)
install(TARGETS afterimage RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(AFTERIMAGE_BUILD_TESTS)
    enable_testing()

    function(afterimage_add_test target_name test_name source_file)
        add_executable(${target_name} ${source_file})
        target_link_libraries(${target_name} PRIVATE afterimage_core)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
            target_compile_options(${target_name} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${test_name} COMMAND ${target_name})
    endfunction()

    afterimage_add_test(afterimage_tests DocumentManagerTests tests/unit/DocumentManagerTests.cpp)
    afterimage_add_test(afterimage_ingestion_tests IngestionTests tests/unit/IngestionTests.cpp)
    afterimage_add_test(afterimage_storage_tests StorageTests tests/unit/StorageTests.cpp)
    afterimage_add_test(afterimage_indexing_tests IndexingTests tests/unit/IndexingTests.cpp)
    afterimage_add_test(afterimage_retrieval_tests RetrievalTests tests/unit/RetrievalTests.cpp)
    afterimage_add_test(afterimage_ranking_tests RankingTests tests/unit/RankingTests.cpp)
    afterimage_add_test(afterimage_graph_tests GraphTests tests/unit/GraphTests.cpp)

    add_executable(afterimage_extraction_tests tests/unit/ExtractionTests.cpp)
    target_link_libraries(afterimage_extraction_tests PRIVATE afterimage_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
        target_compile_options(afterimage_extraction_tests PRIVATE -UNDEBUG)
    endif()
    target_compile_definitions(afterimage_extraction_tests PRIVATE
        AFTERIMAGE_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    add_test(NAME ExtractionTests COMMAND afterimage_extraction_tests)

    add_executable(afterimage_pipeline_integration_tests tests/integration/PipelineIntegrationTests.cpp)
    target_link_libraries(afterimage_pipeline_integration_tests PRIVATE afterimage_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
        target_compile_options(afterimage_pipeline_integration_tests PRIVATE -UNDEBUG)
    endif()
    target_compile_definitions(afterimage_pipeline_integration_tests PRIVATE
        AFTERIMAGE_TEST_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/advanced_test_corpus"
    )
    add_test(NAME PipelineIntegrationTests COMMAND afterimage_pipeline_integration_tests)

    add_test(NAME CommandLineHelp COMMAND afterimage --help)

    add_test(NAME CommandLineWorkflow
        COMMAND ${CMAKE_COMMAND}
            -DAFTERIMAGE_BIN=$<TARGET_FILE:afterimage>
            -DAFTERIMAGE_CORPUS=${CMAKE_CURRENT_SOURCE_DIR}/data/advanced_test_corpus
            -DAFTERIMAGE_DB=${CMAKE_CURRENT_BINARY_DIR}/cli_workflow_test.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/CliWorkflowTest.cmake
    )

    add_test(NAME CommandLineMediaWorkflow
        COMMAND ${CMAKE_COMMAND}
            -DAFTERIMAGE_BIN=$<TARGET_FILE:afterimage>
            -DAFTERIMAGE_FIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
            -DAFTERIMAGE_DB=${CMAKE_CURRENT_BINARY_DIR}/cli_media_workflow_test.db
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/CliMediaWorkflowTest.cmake
    )
endif()
