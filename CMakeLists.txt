cmake_minimum_required(VERSION 3.16)
project(felidae_interpreter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8)
    message(FATAL_ERROR "Felidae requires GCC 8 or newer for std::filesystem. Use GCC 8+, Clang 7+, AppleClang 10+, or MSVC 2019+.")
endif()

add_executable(felidae
    src/main.cpp
    src/FelidaeRuntime.cpp
    src/Visualization.cpp
    src/BuiltinRegistry.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/Interpreter.cpp
    src/Env.cpp
    src/Memory.cpp
    src/NativeRuntime.cpp
    native_modules/csv/NativeCsv.cpp
    native_modules/http/NativeHttp.cpp
    native_modules/process/NativeProcess.cpp
)

target_include_directories(felidae PRIVATE src)
target_include_directories(felidae SYSTEM PRIVATE third_party)
target_compile_options(felidae PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(felidae PRIVATE dl)
endif()

set(CELIDAE_SOURCES
    src/felidae_debug.cpp
    src/FelidaeRuntime.cpp
    src/Visualization.cpp
    src/BuiltinRegistry.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/Interpreter.cpp
    src/Env.cpp
    src/Memory.cpp
    src/NativeRuntime.cpp
    src/AstAnalyzer.cpp
    native_modules/csv/NativeCsv.cpp
    native_modules/http/NativeHttp.cpp
    native_modules/process/NativeProcess.cpp
)

add_executable(celidae ${CELIDAE_SOURCES})
target_include_directories(celidae PRIVATE src)
target_include_directories(celidae SYSTEM PRIVATE third_party)
target_compile_options(celidae PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(celidae PRIVATE dl)
endif()

# Legacy target kept so existing editor/plugin installations do not break while
# Celidae becomes the product name for debugger, analytics, and visualization.
add_executable(felidae_debug ${CELIDAE_SOURCES})
target_include_directories(felidae_debug PRIVATE src)
target_include_directories(felidae_debug SYSTEM PRIVATE third_party)
target_compile_options(felidae_debug PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(felidae_debug PRIVATE dl)
endif()

add_library(felidae_smoke SHARED native_modules/smoke/NativeSmoke.cpp)
set_target_properties(felidae_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/smoke"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/smoke"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/smoke"
)

add_library(wordnet SHARED native_modules/wordnet/NativeWordNet.cpp)
target_include_directories(wordnet PRIVATE native_modules/wordnet)
set_target_properties(wordnet PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/wordnet"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/wordnet"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/wordnet"
)

add_library(fact SHARED native_modules/fact/NativeFact.cpp)
target_include_directories(fact PRIVATE native_modules/fact)
set_target_properties(fact PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact"
)

add_library(fact_analysis SHARED native_modules/fact/NativeFact.cpp)
target_include_directories(fact_analysis PRIVATE native_modules/fact)
set_target_properties(fact_analysis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact_analysis"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact_analysis"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/native_modules/fact_analysis"
)

find_package(Eigen3 QUIET)
if(Eigen3_FOUND)
    target_compile_definitions(felidae PRIVATE FELIDAE_HAS_EIGEN=1)
    target_link_libraries(felidae PRIVATE Eigen3::Eigen)
    target_compile_definitions(celidae PRIVATE FELIDAE_HAS_EIGEN=1)
    target_link_libraries(celidae PRIVATE Eigen3::Eigen)
    target_compile_definitions(felidae_debug PRIVATE FELIDAE_HAS_EIGEN=1)
    target_link_libraries(felidae_debug PRIVATE Eigen3::Eigen)
endif()
