cmake_minimum_required(VERSION 3.25)
project(ChessTrainer VERSION 1.2.2 LANGUAGES C CXX)
file(WRITE ${CMAKE_BINARY_DIR}/version.iss "#define AppVer \"${PROJECT_VERSION}\"")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
FetchContent_MakeAvailable(raylib)

FetchContent_Declare(sqlite
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite)
add_library(sqlite3 STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1)

FetchContent_Declare(zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(zstd)
file(GLOB ZSTD_SOURCES ${zstd_SOURCE_DIR}/lib/common/*.c ${zstd_SOURCE_DIR}/lib/compress/*.c ${zstd_SOURCE_DIR}/lib/decompress/*.c)
add_library(zstd STATIC ${ZSTD_SOURCES})
target_include_directories(zstd SYSTEM PUBLIC ${zstd_SOURCE_DIR}/lib)
target_compile_definitions(zstd PRIVATE ZSTD_DISABLE_ASM)

if(MSVC)
    set(PROJECT_WARNINGS /W4 /permissive- /utf-8 /MP)
else()
    set(PROJECT_WARNINGS -Wall -Wextra -Wpedantic)
endif()

add_library(chess_core STATIC
    src/core/position.cpp
    src/core/notation.cpp
    src/core/pgn.cpp
)
target_include_directories(chess_core PUBLIC src)
target_compile_options(chess_core PRIVATE ${PROJECT_WARNINGS})

add_library(chess_engine STATIC
    src/engine/eval.cpp
    src/engine/search.cpp
    src/engine/bots.cpp
    src/coach/coach.cpp
)
target_link_libraries(chess_engine PUBLIC chess_core)
target_compile_options(chess_engine PRIVATE ${PROJECT_WARNINGS})

add_library(chess_profile STATIC
    src/rating/elo.cpp
    src/storage/profile.cpp
    src/storage/progress.cpp
)
target_include_directories(chess_profile PUBLIC src)
target_link_libraries(chess_profile PUBLIC sqlite3)
target_compile_options(chess_profile PRIVATE ${PROJECT_WARNINGS})

add_library(chess_training STATIC
    src/training/puzzles.cpp
    src/training/puzzle_import.cpp
    src/training/download.cpp
    src/training/update.cpp
    src/training/lessons.cpp
    src/training/endgames.cpp
    src/training/openings.cpp
    src/training/vision.cpp
)
target_link_libraries(chess_training PUBLIC chess_core zstd winhttp shell32)
target_compile_options(chess_training PRIVATE ${PROJECT_WARNINGS})

add_library(chess_analysis STATIC
    src/uci/uci_engine.cpp
    src/analysis/review.cpp
    src/analysis/explain.cpp
    src/analysis/analyzer.cpp
    src/analysis/weakness.cpp
)
target_link_libraries(chess_analysis PUBLIC chess_engine)
target_compile_options(chess_analysis PRIVATE ${PROJECT_WARNINGS})

add_executable(ChessTrainer WIN32
    src/main.cpp
    src/app.rc
    src/ui/app.cpp
    src/ui/app_analysis.cpp
    src/ui/app_coach.cpp
    src/ui/app_lessons.cpp
    src/ui/app_openings.cpp
    src/ui/app_endgames.cpp
    src/ui/app_plan.cpp
    src/ui/app_chess960.cpp
    src/ui/app_puzzles.cpp
    src/ui/app_shots.cpp
    src/ui/app_update.cpp
    src/ui/app_vision.cpp
    src/ui/fx.cpp
    src/ui/app_weakness.cpp
    src/ui/board_view.cpp
    src/ui/game_session.cpp
    src/ui/grades.cpp
    src/ui/pieces.cpp
    src/ui/settings.cpp
    src/ui/sound.cpp
    src/ui/theme.cpp
    src/ui/titlebar.cpp
    src/ui/ui.cpp
)
target_include_directories(ChessTrainer PRIVATE src)
target_include_directories(ChessTrainer SYSTEM PRIVATE third_party/nanosvg)
target_link_libraries(ChessTrainer PRIVATE chess_engine chess_profile chess_analysis chess_training raylib shell32 ole32 dwmapi)
target_compile_options(ChessTrainer PRIVATE ${PROJECT_WARNINGS})
target_compile_definitions(ChessTrainer PRIVATE _CRT_SECURE_NO_WARNINGS CHESS_VERSION="${PROJECT_VERSION}"
    CHESS_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets")
if(MSVC)
    target_link_options(ChessTrainer PRIVATE /ENTRY:mainCRTStartup)
endif()
if(MSVC)
    set_source_files_properties(src/ui/pieces.cpp PROPERTIES COMPILE_OPTIONS /wd4702)
endif()
add_custom_command(TARGET ChessTrainer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:ChessTrainer>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/third_party/stockfish $<TARGET_FILE_DIR:ChessTrainer>/stockfish)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE chess_core)
target_compile_options(core_tests PRIVATE ${PROJECT_WARNINGS})
add_test(NAME core_tests COMMAND core_tests)
add_executable(engine_tests tests/engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE chess_engine)
target_compile_options(engine_tests PRIVATE ${PROJECT_WARNINGS})
add_test(NAME engine_tests COMMAND engine_tests)
add_executable(rating_tests tests/rating_tests.cpp)
target_link_libraries(rating_tests PRIVATE chess_profile chess_engine)
target_compile_options(rating_tests PRIVATE ${PROJECT_WARNINGS})
add_test(NAME rating_tests COMMAND rating_tests)
add_executable(analysis_tests tests/analysis_tests.cpp)
target_link_libraries(analysis_tests PRIVATE chess_analysis)
target_compile_options(analysis_tests PRIVATE ${PROJECT_WARNINGS})
target_compile_definitions(analysis_tests PRIVATE CHESS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME analysis_tests COMMAND analysis_tests)
add_executable(training_tests tests/training_tests.cpp)
target_link_libraries(training_tests PRIVATE chess_training chess_profile chess_engine)
target_compile_options(training_tests PRIVATE ${PROJECT_WARNINGS})
target_compile_definitions(training_tests PRIVATE CHESS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME training_tests COMMAND training_tests)
add_executable(openings_check EXCLUDE_FROM_ALL tests/openings_check.cpp)
target_link_libraries(openings_check PRIVATE chess_training chess_analysis)
target_compile_options(openings_check PRIVATE ${PROJECT_WARNINGS})
target_compile_definitions(openings_check PRIVATE CHESS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
