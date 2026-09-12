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
  target_include_directories(${target} PUBLIC
    ${CMAKE_SOURCE_DIR}/src)
  # NativeJson.h parses through the real, vendored nlohmann::json library
  # (single-header form: no separate build target needed) rather than a
  # bespoke recursive-descent parser. csv.parse/csv.toFacts likewise parse
  # through the vendored, previously-unused rapidcsv (handles quoted fields
  # and escaped separators correctly, which a hand-rolled comma-split would
  # not) instead of a bespoke CSV splitter.
  target_include_directories(${target} SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/single_include
    ${CMAKE_SOURCE_DIR}/third_party/rapidcsv/src)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${target} PRIVATE dl)
  endif()
  if(FELIDAE_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
  if(FELIDAE_ENABLE_STRICT_WARNINGS)
    target_compile_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>)
  endif()
endfunction()

set(FELIDAE_RUNTIME_SOURCES
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

add_library(felidae_runtime STATIC ${FELIDAE_RUNTIME_SOURCES})
felidae_common(felidae_runtime)
# WordVocabulary (src/Tokenizer.h) is a fixed, compile-time, byte-level
# vocabulary now - no model file on disk, so no path needs defining here.

add_executable(felidae
  src/main.cpp
  src/DebugSession.cpp
  src/debugger/main.cpp
  src/debugger/AstAnalyzer.cpp)
target_link_libraries(felidae PRIVATE felidae_runtime)
felidae_common(felidae)

if(FELIDAE_BUILD_TESTS)
  enable_testing()
  add_executable(felidae_interpreter_tests tests/InterpreterTests.cpp)
  target_link_libraries(felidae_interpreter_tests PRIVATE felidae_runtime)
  felidae_common(felidae_interpreter_tests)
  add_test(NAME interpreter_unit COMMAND felidae_interpreter_tests)

  add_test(NAME direct_ast_smoke
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_ast_smoke.fx)
  set_tests_properties(direct_ast_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "answer: 42.*literal: 42.*decimal: 3.14")

  add_test(NAME direct_ast_fact_query
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_ast_smoke.fx "? fact(name: x)")
  set_tests_properties(direct_ast_fact_query PROPERTIES
    PASS_REGULAR_EXPRESSION "x = \"tiger\"")

  add_test(NAME direct_ast_fact_mutation
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_fact_mutation.fx)
  set_tests_properties(direct_ast_fact_mutation PROPERTIES
    PASS_REGULAR_EXPRESSION "inserted:.*Temporary.*updated: 1.*deleted: 1.*active: 0")

  add_test(NAME immutable_nil_reassignment
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/invalid_immutable_nil_reassignment.fx)
  set_tests_properties(immutable_nil_reassignment PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME dynamic_fact_from_function
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/dynamic_fact_from_function.fx)
  set_tests_properties(dynamic_fact_from_function PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 1.*Function School")

  add_test(NAME dynamic_fact_rejects_invalid_schema
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/invalid_dynamic_fact_schema.fx)
  set_tests_properties(dynamic_fact_rejects_invalid_schema PROPERTIES
    WILL_FAIL TRUE)

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

  add_test(NAME class_method_chain
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/class_method_chain.fx)
  set_tests_properties(class_method_chain PROPERTIES
    PASS_REGULAR_EXPRESSION "10")

  add_test(NAME class_inherited_constructor
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/class_inherited_constructor.fx)
  set_tests_properties(class_inherited_constructor PROPERTIES
    PASS_REGULAR_EXPRESSION "inherited_field: \"Ada\".*inherited_method: \"Ada\".*grade: 11")

  add_test(NAME tooling_check_json
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/direct_ast_smoke.fx --check-json)
  set_tests_properties(tooling_check_json PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[.*\\]")

  add_test(NAME tooling_symbols_json
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/tooling_import_root.fx --symbols-json --load-imports)
  set_tests_properties(tooling_symbols_json PROPERTIES
    PASS_REGULAR_EXPRESSION "\"ImportedFact\"")

  add_test(NAME tooling_lists_builtins
    COMMAND felidae --list-builtins)
  set_tests_properties(tooling_lists_builtins PROPERTIES
    PASS_REGULAR_EXPRESSION "\"name\".*\"effect\"")

  add_test(NAME tooling_rejects_invalid_source
    COMMAND felidae ${CMAKE_SOURCE_DIR}/tests/invalid_syntax.fx --check-json)
  set_tests_properties(tooling_rejects_invalid_source PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME analytical_query_language_tour
    COMMAND felidae ${CMAKE_SOURCE_DIR}/v2_examples/felidae_language_tour.fx)
  add_test(NAME analytical_indexed_fact_query
    COMMAND felidae ${CMAKE_SOURCE_DIR}/v2_examples/selective_fact_query.fx)
  add_test(NAME expert_system_fuzzy_logic
    COMMAND felidae ${CMAKE_SOURCE_DIR}/v2_examples/fuzzy_logic_basics.fx)
  add_test(NAME expert_system_ancestry_reasoning
    COMMAND felidae ${CMAKE_SOURCE_DIR}/v2_examples/ancestor_relatedness_analysis.fx)
endif()
