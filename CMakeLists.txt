cmake_minimum_required(VERSION 3.21)
project(felidae LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FELIDAE_BUILD_TESTS "Build Felidae regression tests" ON)
option(FELIDAE_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(FELIDAE_ENABLE_STRICT_WARNINGS "Enable strict warnings" OFF)

function(felidae_common target)
  target_include_directories(${target} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/models)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${target} PRIVATE dl)
  endif()
  if(FELIDAE_ENABLE_SANITIZERS)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
endfunction()

add_executable(felidae
  src/main.cpp
  src/FelidaeRuntime.cpp
  src/IntegerParser.cpp
  src/IntegerTokenList.cpp
  src/Tokenizer.cpp
  src/BuiltinRegistry.cpp
  src/Interpreter.cpp
  src/ReasoningRuntime.cpp
  src/Env.cpp
  src/Memory.cpp
  src/NativeRuntime.cpp)
felidae_common(felidae)
target_compile_definitions(felidae PRIVATE
  FELIDAE_TOKENIZER_MODEL_PATH="${CMAKE_SOURCE_DIR}/models/felidae-bpe/model.txt")
target_compile_options(felidae PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>)

if(FELIDAE_BUILD_TESTS)
  enable_testing()
  add_test(NAME direct_ast_smoke
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_ast_smoke.fx)
  set_tests_properties(direct_ast_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "answer: 42.*literal: 42.*decimal: 3.14")

  add_test(NAME direct_ast_fact_query
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_ast_smoke.fx "? fact(name: x)")
  set_tests_properties(direct_ast_fact_query PROPERTIES
    PASS_REGULAR_EXPRESSION "x = \"tiger\"")

  add_test(NAME direct_ast_rejects_binary
    COMMAND felidae ${CMAKE_BINARY_DIR}/legacy.bin)
  set_tests_properties(direct_ast_rejects_binary PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME direct_ast_fact_mutation
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_fact_mutation.fx)
  set_tests_properties(direct_ast_fact_mutation PROPERTIES
    PASS_REGULAR_EXPRESSION "inserted:.*Temporary.*updated: 1.*deleted: 1.*active: 0")

  add_test(NAME direct_ast_db_sync
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_db_sync.fx)
  set_tests_properties(direct_ast_db_sync PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    PASS_REGULAR_EXPRESSION "synchronized: 1.*Synced")

  add_test(NAME direct_ast_fact_selection
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_fact_selection.fx)
  set_tests_properties(direct_ast_fact_selection PROPERTIES
    PASS_REGULAR_EXPRESSION "or_count: 3.*and_count: 1.*limited: [[]School")

  add_test(NAME direct_ast_mixfix
    COMMAND felidae ${CMAKE_SOURCE_DIR}/v2_examples/mixfix_nested_expression.fx)
  set_tests_properties(direct_ast_mixfix PROPERTIES
    PASS_REGULAR_EXPRESSION "direct: Validation")

  add_test(NAME direct_ast_class_end
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_class_end.fx)
  set_tests_properties(direct_ast_class_end PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 1.*answer: 42")

  add_test(NAME direct_ast_class_mixfix
    COMMAND felidae ${CMAKE_SOURCE_DIR}/v2_examples/class_association_reasoning.fx)
  set_tests_properties(direct_ast_class_mixfix PROPERTIES
    PASS_REGULAR_EXPRESSION "Association.*works at")

  add_test(NAME direct_ast_class_fact_value
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_class_fact_value.fx)
  set_tests_properties(direct_ast_class_fact_value PROPERTIES
    PASS_REGULAR_EXPRESSION "person: Person.*from_array: Person.*name: \"Ada\"")
endif()
