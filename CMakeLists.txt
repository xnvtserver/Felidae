cmake_minimum_required(VERSION 3.21)
project(felidae LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FELIDAE_BUILD_TESTS "Build Felidae regression tests" ON)
option(FELIDAE_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(FELIDAE_ENABLE_STRICT_WARNINGS "Enable strict warnings" OFF)
option(FELIDAE_USE_SYSTEM_ROCKSDB "Use an installed RocksDB package" OFF)
set(FELIDAE_DEPENDENCY_JOBS "1" CACHE STRING
    "Maximum parallel compiler processes used inside vendored dependencies")
if(NOT FELIDAE_DEPENDENCY_JOBS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "FELIDAE_DEPENDENCY_JOBS must be a positive integer")
endif()

# RocksDB is distributed under Apache-2.0 or GPLv2. Felidae consumes it under
# Apache-2.0. Keep the version pinned so the durable format and build remain
# reproducible on Windows, Linux, and macOS.
if(FELIDAE_USE_SYSTEM_ROCKSDB)
  find_package(RocksDB 11.8.1 CONFIG REQUIRED)
else()
  set(WITH_TESTS OFF CACHE BOOL "" FORCE)
  set(WITH_BENCHMARK_TOOLS OFF CACHE BOOL "" FORCE)
  set(WITH_TOOLS OFF CACHE BOOL "" FORCE)
  set(WITH_CORE_TOOLS OFF CACHE BOOL "" FORCE)
  set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
  set(WITH_SNAPPY OFF CACHE BOOL "" FORCE)
  set(WITH_LZ4 OFF CACHE BOOL "" FORCE)
  set(WITH_ZSTD OFF CACHE BOOL "" FORCE)
  set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
  set(WITH_BZ2 OFF CACHE BOOL "" FORCE)
  # Build a baseline CPU-compatible RocksDB. Its default PORTABLE=0 probes the
  # build machine and selected /arch:AVX2 on current MSVC, producing binaries
  # that can fail on older x64 processors.
  set(PORTABLE 1 CACHE STRING "" FORCE)
  # RocksDB enables /WX by default. New MSVC releases can introduce warnings
  # in the pinned upstream source. Dependency diagnostics must not make a
  # Felidae build fail; FELIDAE_ENABLE_STRICT_WARNINGS governs our own targets
  # independently through felidae_common().
  set(FAIL_ON_WARNINGS OFF CACHE BOOL "" FORCE)
  # RocksDB's supported MSVC fast-build mode enables /MP, optimized Debug
  # dependency objects, FASTLINK, and its maintained warning suppressions.
  # This affects only the vendored dependency, not Felidae Debug code.
  if(MSVC)
    set(WIN_CI ON CACHE BOOL "" FORCE)
  endif()
  set(ROCKSDB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(FELIDAE_ROCKSDB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/rocksdb")
  if(NOT EXISTS "${FELIDAE_ROCKSDB_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "RocksDB submodule is missing. Run: git submodule update --init --recursive")
  endif()
  add_subdirectory(
    "${FELIDAE_ROCKSDB_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/rocksdb-build"
    EXCLUDE_FROM_ALL)
  # Treat vendored RocksDB as an external dependency. Its pinned source is not
  # maintained in this repository, so keep its warnings out of normal build
  # output while retaining Felidae's own /W4 or -Wall diagnostics.
  if(MSVC AND TARGET rocksdb)
    target_compile_options(rocksdb PRIVATE
      /w
      "/MP${FELIDAE_DEPENDENCY_JOBS}")
  endif()
endif()

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
  if(FELIDAE_ENABLE_SANITIZERS)
    if(MSVC)
      target_compile_options(${target} PRIVATE /fsanitize=address /Zi /RTC-)
      target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    else()
      message(FATAL_ERROR "FELIDAE_ENABLE_SANITIZERS is unsupported by this compiler")
    endif()
  endif()
  if(FELIDAE_ENABLE_STRICT_WARNINGS)
    target_compile_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>)
  endif()
  if(MSVC)
    # Parallel cl.exe processes share the target PDB. /FS serializes PDB
    # writes through MSVC's compiler server and prevents intermittent C1041
    # failures when build.cmd is invoked with --jobs greater than one.
    target_compile_options(${target} PRIVATE /bigobj /FS)
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
  src/TypeHierarchy.cpp
  src/RocksFactStore.cpp
  src/NativeRuntime.cpp)

add_library(felidae_runtime STATIC ${FELIDAE_RUNTIME_SOURCES})
felidae_common(felidae_runtime)
if(TARGET RocksDB::rocksdb)
  target_link_libraries(felidae_runtime PRIVATE RocksDB::rocksdb)
else()
  target_link_libraries(felidae_runtime PRIVATE rocksdb)
endif()

add_executable(felidae
  src/main.cpp
  src/ProjectConfiguration.cpp
  src/DebugSession.cpp
  src/ReplLineEditor.cpp
  src/TerminalUi.cpp)
target_link_libraries(felidae PRIVATE felidae_runtime)
felidae_common(felidae)

if(FELIDAE_BUILD_TESTS)
  enable_testing()
  add_executable(felidae_storage_tests tests/InterpreterTests.cpp)
  target_link_libraries(felidae_storage_tests PRIVATE felidae_runtime)
  felidae_common(felidae_storage_tests)
  add_test(NAME storage_key_encoding_unit COMMAND felidae_storage_tests)
  set(FELIDAE_FX_TEST_COMMAND
    ${CMAKE_COMMAND}
    -DFELIDAE_EXECUTABLE=$<TARGET_FILE:felidae>
    -DFELIDAE_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-projects
    -P ${CMAKE_SOURCE_DIR}/cmake/RunFelidaeProjectTest.cmake)

  add_test(NAME direct_ast_smoke
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/direct_ast_smoke.fx)
  set_tests_properties(direct_ast_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "answer: 42.*literal: 42.*decimal: 3.14")

  add_test(NAME strict_statement_terminators
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/statement_terminators.fx)
  set_tests_properties(strict_statement_terminators PROPERTIES
    PASS_REGULAR_EXPRESSION "42")

  foreach(invalid_case IN ITEMS
      missing_statement_period
      missing_top_level_period
      period_after_end
      leading_member_access
      double_member_dot)
    add_test(NAME syntax_${invalid_case}_rejected
      COMMAND ${FELIDAE_FX_TEST_COMMAND}
        ${CMAKE_SOURCE_DIR}/tests/invalid/${invalid_case}.fx)
    set_tests_properties(syntax_${invalid_case}_rejected PROPERTIES
      WILL_FAIL TRUE)
  endforeach()

  add_test(NAME standalone_repl
    COMMAND ${CMAKE_COMMAND}
      -DFELIDAE_EXE=$<TARGET_FILE:felidae>
      -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/repl-test
      -P ${CMAKE_SOURCE_DIR}/cmake/RunReplTest.cmake)

  add_test(NAME project_configuration
    COMMAND ${CMAKE_COMMAND}
      -DFELIDAE_EXECUTABLE=$<TARGET_FILE:felidae>
      -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/project-configuration-test
      -P ${CMAKE_SOURCE_DIR}/cmake/RunProjectConfigurationTest.cmake)

  add_test(NAME invalid_syntax_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_syntax.fx)
  set_tests_properties(invalid_syntax_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME missing_def_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_missing_def.fx)
  set_tests_properties(missing_def_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME missing_function_end_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_missing_function_end.fx)
  set_tests_properties(missing_function_end_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME missing_if_end_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_missing_if_end.fx)
  set_tests_properties(missing_if_end_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME namespace_separator_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_namespace_separator.fx)
  set_tests_properties(namespace_separator_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME direct_ast_fact_query
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/direct_ast_smoke.fx "? Animal(name: x).")
  set_tests_properties(direct_ast_fact_query PROPERTIES
    PASS_REGULAR_EXPRESSION "x = \"tiger\"")

  add_test(NAME durable_solver_backtracking
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/solver_durable_backtracking.fx
            "? Candidate(id: x), x == \"b\".")
  set_tests_properties(durable_solver_backtracking PROPERTIES
    PASS_REGULAR_EXPRESSION "x = \"b\"")

  add_test(NAME relational_rule_binds_output
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/relational_rule_binds_output.fx
            "? pick(x: \"a\", y: v), v == \"b\".")
  set_tests_properties(relational_rule_binds_output PROPERTIES
    PASS_REGULAR_EXPRESSION "Solution 1: v = \"b\""
    FAIL_REGULAR_EXPRESSION "v = v|Solution 2")

  add_test(NAME rocks_reasoning_facts
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_reasoning_facts.fx)
  set_tests_properties(rocks_reasoning_facts PROPERTIES
    PASS_REGULAR_EXPRESSION "truth: \"proved\".*support: 1.*provenance: 1.*rules: 1")

  add_test(NAME direct_ast_fact_mutation
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/direct_fact_mutation.fx)
  set_tests_properties(direct_ast_fact_mutation PROPERTIES
    PASS_REGULAR_EXPRESSION "inserted:.*Temporary.*updated: 1.*deleted: 1.*active: 0")

  add_test(NAME immutable_nil_reassignment
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_immutable_nil_reassignment.fx)
  set_tests_properties(immutable_nil_reassignment PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME dynamic_fact_from_function
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/dynamic_fact_from_function.fx)
  set_tests_properties(dynamic_fact_from_function PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 1.*Function School")

  add_test(NAME dynamic_fact_rejects_invalid_schema
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_dynamic_fact_schema.fx)
  set_tests_properties(dynamic_fact_rejects_invalid_schema PROPERTIES
    WILL_FAIL TRUE)

  add_test(NAME direct_ast_db_sync
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/direct_db_sync.fx)
  set_tests_properties(direct_ast_db_sync PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    PASS_REGULAR_EXPRESSION "synchronized: 1.*Synced")

  add_test(NAME direct_ast_fact_selection
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/direct_fact_selection.fx)
  set_tests_properties(direct_ast_fact_selection PROPERTIES
    PASS_REGULAR_EXPRESSION "or_count: 3.*and_count: 1.*limited: [[]School")

  add_test(NAME indexed_filtered_limit
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/indexed_filtered_limit.fx)
  set_tests_properties(indexed_filtered_limit PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 1.*id: \"b\"")

  add_test(NAME fact_aggregates
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/fact_aggregates.fx)
  set_tests_properties(fact_aggregates PROPERTIES
    PASS_REGULAR_EXPRESSION "total: 120.*north_total: 30.*average: 40.*minimum: 10.*maximum: 90")

  add_test(NAME direct_ast_mixfix
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/v2_examples/mixfix_nested_expression.fx)
  set_tests_properties(direct_ast_mixfix PROPERTIES
    PASS_REGULAR_EXPRESSION "direct: Validation")

  add_test(NAME imported_operator_shared_tokenizer
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/imported_operator_tokenizer.fx)
  set_tests_properties(imported_operator_shared_tokenizer PROPERTIES
    PASS_REGULAR_EXPRESSION "result: 42")

  add_test(NAME mixfix_type_overload
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/v2_examples/mixfix_type_overload.fx)
  set_tests_properties(mixfix_type_overload PROPERTIES
    PASS_REGULAR_EXPRESSION "number_result: 42.*string_result: \"string-overload\"")

  add_test(NAME direct_ast_class_end
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/direct_class_end.fx)
  set_tests_properties(direct_ast_class_end PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 1.*answer: 42")

  add_test(NAME direct_ast_class_mixfix
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/v2_examples/class_association_reasoning.fx)
  set_tests_properties(direct_ast_class_mixfix PROPERTIES
    PASS_REGULAR_EXPRESSION "Association.*works at")

  add_test(NAME direct_ast_class_fact_value
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/direct_class_fact_value.fx)
  set_tests_properties(direct_ast_class_fact_value PROPERTIES
    PASS_REGULAR_EXPRESSION "person: Person.*from_array: Person.*name: \"Ada\"")

  add_test(NAME class_method_chain
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/class_method_chain.fx)
  set_tests_properties(class_method_chain PROPERTIES
    PASS_REGULAR_EXPRESSION "10")

  add_test(NAME class_object_identity
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/class_object_identity.fx)
  set_tests_properties(class_object_identity PROPERTIES
    PASS_REGULAR_EXPRESSION "original: 4.*returned: 4.*retrieved: 4")

  add_test(NAME class_function_field
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/class_function_field.fx)
  set_tests_properties(class_function_field PROPERTIES
    PASS_REGULAR_EXPRESSION "transient: \"computed\".*stored: \"computed\".*same_identity: true")

  add_test(NAME super_parent_graph
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/super_parent_graph.fx)
  set_tests_properties(super_parent_graph PROPERTIES
    PASS_REGULAR_EXPRESSION "description: \"Ada:Research\".*parent_id: \"employee-1\".*root_id: \"employee-1\"")

  add_test(NAME class_field_types_and_mutation
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/class_field_types_and_mutation.fx)
  set_tests_properties(class_field_types_and_mutation PROPERTIES
    PASS_REGULAR_EXPRESSION "label: \"after\".*changed_label: \"after\".*enabled: true.*note: nil.*score_count: 0.*pair_count: 2.*held_label: \"after\".*stored_pair_count: 2")

  add_test(NAME typed_local_collections
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/typed_local_collections.fx)
  set_tests_properties(typed_local_collections PROPERTIES
    PASS_REGULAR_EXPRESSION "number_count: 3.*pair_count: 2.*node_id: \"node-1\".*missing: nil.*payload_count: 2")

  add_test(NAME typed_local_element_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_typed_local_element.fx)
  set_tests_properties(typed_local_element_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME logic_library
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/logic_library.fx)
  set_tests_properties(logic_library PROPERTIES
    PASS_REGULAR_EXPRESSION "antecedent: \"wet\".*consequent: \"rain\".*negated_antecedent: \"wet\".*negated_consequent: \"rain\".*positive: \"wet\".*negative: \"dry\"")

  add_test(NAME optional_primary_key_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_optional_key.fx)
  set_tests_properties(optional_primary_key_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME nested_list_type_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_nested_list_type.fx)
  set_tests_properties(nested_list_type_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME field_assignment_requires_this
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_field_reassignment.fx)
  set_tests_properties(field_assignment_requires_this PROPERTIES WILL_FAIL TRUE)

  add_test(NAME class_graph
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/class_graph.fx)
  set_tests_properties(class_graph PROPERTIES
    PASS_REGULAR_EXPRESSION "inherited_neighbor_count: 1.*adjacency_count: 1.*direct_count: 2")

  add_test(NAME graph_cursor_iteration
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/graph_cursor_iteration.fx)
  set_tests_properties(graph_cursor_iteration PROPERTIES
    PASS_REGULAR_EXPRESSION "edge_count: 2.*seen_count: 2")

  add_test(NAME class_inherited_constructor
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/class_inherited_constructor.fx)
  set_tests_properties(class_inherited_constructor PROPERTIES
    PASS_REGULAR_EXPRESSION "inherited_field: \"Ada\".*inherited_method: \"Ada\".*grade: 11")

  add_test(NAME fact_extends_class_chain
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/fact_extends_class_chain.fx)
  set_tests_properties(fact_extends_class_chain PROPERTIES
    PASS_REGULAR_EXPRESSION "id: \"employee-1\".*name: \"Ada\".*role: \"analyst\".*visible_as_entity: 1")

  add_test(NAME class_extends_fact_chain
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/class_extends_fact_chain.fx)
  set_tests_properties(class_extends_fact_chain PROPERTIES
    PASS_REGULAR_EXPRESSION "inherited_fact_field: \"root\".*inherited_class_field: 2.*own_field: \"mixed\".*visible_as_root: 2")

  add_test(NAME recursive_mixed_inheritance_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/recursive_mixed_cycle.fx)
  set_tests_properties(recursive_mixed_inheritance_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME class_key_persistence
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/class_key_persistence.fx)
  set_tests_properties(class_key_persistence PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 2.*saved_name: \"West\".*stored_name: \"West\"")

  add_test(NAME unquoted_identifier_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_unquoted_identifier.fx)
  set_tests_properties(unquoted_identifier_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME control_flow
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/control_flow.fx)
  set_tests_properties(control_flow PROPERTIES
    PASS_REGULAR_EXPRESSION "for_count: 4.*while_count: 2.*middle_count: 1.*fallthrough_count: 1.*default_count: 0")

  add_test(NAME for_non_iterable_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_for_non_iterable.fx)
  set_tests_properties(for_non_iterable_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME non_boolean_while_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_non_boolean_while.fx)
  set_tests_properties(non_boolean_while_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME non_boolean_if_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_non_boolean_if.fx)
  set_tests_properties(non_boolean_if_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME this_outside_class_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_this_outside_class.fx)
  set_tests_properties(this_outside_class_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME class_instance_call_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_class_instance_call.fx)
  set_tests_properties(class_instance_call_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME non_boolean_graph_where_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_graph_where_non_boolean.fx)
  set_tests_properties(non_boolean_graph_where_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME non_boolean_fact_query_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_fact_query_non_boolean.fx)
  set_tests_properties(non_boolean_fact_query_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME source_locator
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/source_locator.fx)
  set_tests_properties(source_locator PROPERTIES
    ENVIRONMENT FELIDAE_TEST_CWD_IS_PROJECT=1
    PASS_REGULAR_EXPRESSION "employee-1")

  add_test(NAME source_locator_line_mismatch_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_source_locator_line.fx)
  set_tests_properties(source_locator_line_mismatch_rejected PROPERTIES
    ENVIRONMENT FELIDAE_TEST_CWD_IS_PROJECT=1
    WILL_FAIL TRUE)

  add_test(NAME tail_call_goal_position
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/tail_call_goal_position.fx)
  set_tests_properties(tail_call_goal_position PROPERTIES
    PASS_REGULAR_EXPRESSION "^100")

  add_test(NAME failed_value_call_runs_once
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/failed_value_call_runs_once.fx)
  set_tests_properties(failed_value_call_runs_once PROPERTIES
    PASS_REGULAR_EXPRESSION "EFFECT\nfalse"
    FAIL_REGULAR_EXPRESSION "EFFECT.*EFFECT")

  add_test(NAME self_containing_field_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_self_containing_field.fx)
  set_tests_properties(self_containing_field_rejected PROPERTIES
    PASS_REGULAR_EXPRESSION "would make an object contain itself")

  add_test(NAME builtin_method_override
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/builtin_method_override.fx)
  set_tests_properties(builtin_method_override PROPERTIES
    PASS_REGULAR_EXPRESSION "overridden_count: 77.*overridden_join: \"custom-join\".*builtin_without_override: 0")

  add_test(NAME duplicate_fact_key_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_duplicate_fact_key.fx)
  set_tests_properties(duplicate_fact_key_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME dotted_related_class
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/dotted_related_class.fx)
  set_tests_properties(dotted_related_class PROPERTIES
    PASS_REGULAR_EXPRESSION "School.Student.*student-1.*grade: 10")

  add_test(NAME dynamic_fact_schema_lock
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/dynamic_fact_schema_lock.fx)
  set_tests_properties(dynamic_fact_schema_lock PROPERTIES
    PASS_REGULAR_EXPRESSION "stored: 2.*transient: 30")

  add_test(NAME undeclared_fact_open_shape
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/undeclared_fact_open_shape.fx)
  set_tests_properties(undeclared_fact_open_shape PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 3.*only_a: 1")

  add_test(NAME undeclared_fact_missing_key_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_undeclared_fact_missing_key.fx)
  set_tests_properties(undeclared_fact_missing_key_rejected PROPERTIES
    PASS_REGULAR_EXPRESSION "Fact key field 'a' is missing")

  add_test(NAME lowercase_fact_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_lowercase_fact.fx)
  set_tests_properties(lowercase_fact_rejected PROPERTIES
    PASS_REGULAR_EXPRESSION "Fact 'ghost' must begin with an uppercase letter at line 3")

  add_test(NAME lowercase_fact_extend_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_lowercase_fact_extend.fx)
  set_tests_properties(lowercase_fact_extend_rejected PROPERTIES
    PASS_REGULAR_EXPRESSION "Fact 'child' must begin with an uppercase letter")

  add_test(NAME fact_namespace_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_fact_namespace.fx)
  set_tests_properties(fact_namespace_rejected PROPERTIES
    PASS_REGULAR_EXPRESSION "'Fact' is not a queryable class; query a class directly, for example Employee.count\\(\\) at line 11")

  add_test(NAME class_count_query
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/class_count_query.fx)
  set_tests_properties(class_count_query PROPERTIES
    PASS_REGULAR_EXPRESSION "^2")

  add_test(NAME thread_sees_facts
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/thread_sees_facts.fx)
  set_tests_properties(thread_sees_facts PROPERTIES
    PASS_REGULAR_EXPRESSION "\"2\"")

  add_test(NAME order_by_numeric
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/order_by_numeric.fx)
  set_tests_properties(order_by_numeric PROPERTIES
    PASS_REGULAR_EXPRESSION "id: \"b\".*id: \"d\".*id: \"c\".*id: \"a\"")

  add_test(NAME order_by_string_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_order_by_string.fx)
  set_tests_properties(order_by_string_rejected PROPERTIES
    PASS_REGULAR_EXPRESSION "order_by supports only numeric values: field 'v' of row 1")

  add_test(NAME order_by_missing_field_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_order_by_missing.fx)
  set_tests_properties(order_by_missing_field_rejected PROPERTIES
    PASS_REGULAR_EXPRESSION "order_by supports only numeric values: field 'v' of row 1 is missing")

  add_test(NAME try_catch_runtime_error
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_runtime_error.fx)
  set_tests_properties(try_catch_runtime_error PROPERTIES
    PASS_REGULAR_EXPRESSION "kind: \"runtime\".*order_by supports only numeric values: field 'v' of row 0 is")

  add_test(NAME try_catch_thrown_exception
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_thrown_exception.fx)
  set_tests_properties(try_catch_thrown_exception PROPERTIES
    PASS_REGULAR_EXPRESSION "kind: \"custom\".*message: \"boom\"")

  add_test(NAME try_catch_no_error
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_no_error.fx)
  set_tests_properties(try_catch_no_error PROPERTIES
    PASS_REGULAR_EXPRESSION "\"completed\"")

  add_test(NAME try_catch_keeps_writes
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_keeps_writes.fx)
  set_tests_properties(try_catch_keeps_writes PROPERTIES
    PASS_REGULAR_EXPRESSION "reason: \"after the write\".*items: 1")

  add_test(NAME throw_uncaught_reports_message
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_throw_uncaught.fx)
  set_tests_properties(throw_uncaught_reports_message PROPERTIES
    PASS_REGULAR_EXPRESSION "error: uncaught boom")

  add_test(NAME throw_without_kind_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_throw_without_kind.fx)
  set_tests_properties(throw_without_kind_rejected PROPERTIES
    PASS_REGULAR_EXPRESSION "throw exception must be an object with a string 'kind' field")

  add_test(NAME try_catch_sequential
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_sequential.fx)
  set_tests_properties(try_catch_sequential PROPERTIES
    PASS_REGULAR_EXPRESSION "first: \"one\".*second: \"two\"")

  add_test(NAME try_catch_nested
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_nested.fx)
  set_tests_properties(try_catch_nested PROPERTIES
    PASS_REGULAR_EXPRESSION "handled: \"inner error\".*outcome: \"outer completed\"")

  add_test(NAME try_catch_error_in_catch
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_error_in_catch.fx)
  set_tests_properties(try_catch_error_in_catch PROPERTIES
    PASS_REGULAR_EXPRESSION "kind: \"second\".*message: \"raised while handling\"")

  add_test(NAME try_catch_rethrow
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_rethrow.fx)
  set_tests_properties(try_catch_rethrow PROPERTIES
    PASS_REGULAR_EXPRESSION "kind: \"disk\".*message: \"full\"")

  add_test(NAME try_catch_first_error_wins
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_first_error_wins.fx)
  set_tests_properties(try_catch_first_error_wins PROPERTIES
    PASS_REGULAR_EXPRESSION "reason: \"first error\".*items: 1")

  add_test(NAME try_catch_by_kind
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_by_kind.fx)
  set_tests_properties(try_catch_by_kind PROPERTIES
    PASS_REGULAR_EXPRESSION "a: \"handled not_found\".*b: \"handled invalid\".*c: \"ok\"")

  add_test(NAME try_catch_cascade
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_cascade.fx)
  set_tests_properties(try_catch_cascade PROPERTIES
    PASS_REGULAR_EXPRESSION "kind: \"c\".*message: \"b\"")

  add_test(NAME try_catch_cascade_stops
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/try_catch_cascade_stops.fx)
  set_tests_properties(try_catch_cascade_stops PROPERTIES
    PASS_REGULAR_EXPRESSION "handled: \"from try\".*items: 0")

  add_test(NAME try_catch_cascade_last_raises
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_try_catch_cascade_last_raises.fx)
  set_tests_properties(try_catch_cascade_last_raises PROPERTIES
    PASS_REGULAR_EXPRESSION "error: last catch failed")

  add_test(NAME rocks_graph_properties
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_graph_properties.fx)
  set_tests_properties(rocks_graph_properties PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 1.*strong: 1.*primary_key_match: 1.*employee: \"Ada\".*department: \"Research\".*since: 2024")

  add_test(NAME invalid_missing_link_target
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_missing_link_target.fx)
  set_tests_properties(invalid_missing_link_target PROPERTIES WILL_FAIL TRUE)

  add_test(NAME rocks_graph_traversal
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_graph_traversal.fx)
  set_tests_properties(rocks_graph_traversal PROPERTIES
    PASS_REGULAR_EXPRESSION "direct_count: 3.*limited_count: 1.*both_count: 2.*recursive_count: 2.*shortest_depth: 2")

  add_test(NAME invalid_graph_property_filter
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_graph_property_filter.fx)
  set_tests_properties(invalid_graph_property_filter PROPERTIES WILL_FAIL TRUE)

  add_test(NAME relationship_class_syntax_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_relationship_class.fx)
  set_tests_properties(relationship_class_syntax_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME link_relationship_argument_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_link_relationship_argument.fx)
  set_tests_properties(link_relationship_argument_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME link_class_name_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_link_class.fx)
  set_tests_properties(link_class_name_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME rocks_graph_sync_cycle
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_graph_sync_cycle.fx)
  set_tests_properties(rocks_graph_sync_cycle PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    PASS_REGULAR_EXPRESSION "synchronized: 2.*direct: 2.*recursive: 1")

  add_test(NAME rocks_graph_sync_preserves_links
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_graph_sync_preserves_links.fx)
  set_tests_properties(rocks_graph_sync_preserves_links PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    PASS_REGULAR_EXPRESSION "first_sync: 2.*changed: 1.*second_sync: 2.*links: 1.*source_label: \"source\"")

  add_test(NAME sync_link_source_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_sync_link.fx)
  set_tests_properties(sync_link_source_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    WILL_FAIL TRUE)

  add_test(NAME rocks_graph_multiple_links
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_graph_multiple_links.fx)
  set_tests_properties(rocks_graph_multiple_links PROPERTIES
    PASS_REGULAR_EXPRESSION "all_links: 5.*connected: 2.*assigned: 2.*reversed: \"second\".*key_match: 1")

  add_test(NAME duplicate_runtime_link_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_duplicate_runtime_link.fx)
  set_tests_properties(duplicate_runtime_link_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME dynamic_link_variables
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/dynamic_link_variables.fx)
  set_tests_properties(dynamic_link_variables PROPERTIES
    PASS_REGULAR_EXPRESSION "created_to: \"department-1\".*count: 1.*confidence: 0.9")

  add_test(NAME rocks_graph_link_survives_update
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_graph_link_survives_update.fx)
  set_tests_properties(rocks_graph_link_survives_update PROPERTIES
    PASS_REGULAR_EXPRESSION "changed: 1.*source: \"after\".*target: \"target\"")

  add_test(NAME rocks_graph_composite_reverse
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_graph_composite_reverse.fx)
  set_tests_properties(rocks_graph_composite_reverse PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 1.*city: \"Bengaluru\".*employee: \"e1\"")

  add_test(NAME rocks_graph_inherited_link
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_graph_inherited_link.fx)
  set_tests_properties(rocks_graph_inherited_link PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 1.*department: \"Research\"")

  add_test(NAME rocks_subtype_visibility
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/rocks_subtype_visibility.fx)
  set_tests_properties(rocks_subtype_visibility PROPERTIES
    PASS_REGULAR_EXPRESSION "entities: 1.*notes: 1")

  add_test(NAME rocks_graph_restrict_delete
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_restrict_delete.fx)
  set_tests_properties(rocks_graph_restrict_delete PROPERTIES WILL_FAIL TRUE)

  add_test(NAME rocks_graph_restrict_key_update
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/invalid_referenced_key_update.fx)
  set_tests_properties(rocks_graph_restrict_key_update PROPERTIES WILL_FAIL TRUE)

  add_test(NAME graph_debugger
    COMMAND ${CMAKE_COMMAND}
      -DFELIDAE_EXECUTABLE=$<TARGET_FILE:felidae>
      -DFELIDAE_SOURCE=${CMAKE_SOURCE_DIR}/tests/debug_graph.fx
      -DFELIDAE_COMMANDS=${CMAKE_SOURCE_DIR}/tests/debug_graph_commands.txt
      -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/graph-debug-test
      -P ${CMAKE_SOURCE_DIR}/cmake/RunGraphDebugTest.cmake)

  set(FELIDAE_PERSISTENCE_TEST_PROJECT
    "${CMAKE_CURRENT_BINARY_DIR}/test-projects/persistent/persistence")
  set(FELIDAE_PERSISTENCE_TEST_DB "${FELIDAE_PERSISTENCE_TEST_PROJECT}/data.db")
  add_test(NAME rocks_persistence_clean
    COMMAND ${CMAKE_COMMAND}
      -DDATABASE_DIRECTORY=${FELIDAE_PERSISTENCE_TEST_DB}
      -P ${CMAKE_SOURCE_DIR}/cmake/CleanFelidaeTestDatabase.cmake)
  set_tests_properties(rocks_persistence_clean PROPERTIES FIXTURES_SETUP persistence_clean)

  add_test(NAME rocks_persistence_seed
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/persistence/rocks_persistence_seed.fx)
  set_tests_properties(rocks_persistence_seed PROPERTIES
    FIXTURES_REQUIRED persistence_clean
    FIXTURES_SETUP persistence_seeded
    PASS_REGULAR_EXPRESSION "1")

  add_test(NAME rocks_persistence_idempotent_reseed
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/persistence/rocks_persistence_reseed.fx)
  set_tests_properties(rocks_persistence_idempotent_reseed PROPERTIES
    FIXTURES_REQUIRED persistence_seeded
    FIXTURES_SETUP persistence_reseeded
    PASS_REGULAR_EXPRESSION "1")

  add_test(NAME rocks_persistence_read
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/persistence/rocks_persistence_read.fx)
  set_tests_properties(rocks_persistence_read PROPERTIES
    FIXTURES_REQUIRED persistence_reseeded
    PASS_REGULAR_EXPRESSION "42")

  set(FELIDAE_SCHEMA_TEST_PROJECT
    "${CMAKE_CURRENT_BINARY_DIR}/test-projects/persistent/schema")
  set(FELIDAE_SCHEMA_TEST_DB "${FELIDAE_SCHEMA_TEST_PROJECT}/data.db")
  add_test(NAME rocks_schema_clean
    COMMAND ${CMAKE_COMMAND}
      -DDATABASE_DIRECTORY=${FELIDAE_SCHEMA_TEST_DB}
      -P ${CMAKE_SOURCE_DIR}/cmake/CleanFelidaeTestDatabase.cmake)
  set_tests_properties(rocks_schema_clean PROPERTIES FIXTURES_SETUP schema_clean)

  add_test(NAME rocks_schema_seed
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/schema/rocks_schema_seed.fx)
  set_tests_properties(rocks_schema_seed PROPERTIES
    FIXTURES_REQUIRED schema_clean
    FIXTURES_SETUP schema_seeded
    PASS_REGULAR_EXPRESSION "1")

  add_test(NAME rocks_schema_open_shape
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/schema/rocks_schema_open_shape.fx)
  set_tests_properties(rocks_schema_open_shape PROPERTIES
    FIXTURES_REQUIRED schema_seeded
    PASS_REGULAR_EXPRESSION "3")

  set(FELIDAE_ATOMIC_DELETE_TEST_PROJECT
    "${CMAKE_CURRENT_BINARY_DIR}/test-projects/persistent/atomic_delete")
  set(FELIDAE_ATOMIC_DELETE_TEST_DB "${FELIDAE_ATOMIC_DELETE_TEST_PROJECT}/data.db")
  add_test(NAME atomic_delete_clean
    COMMAND ${CMAKE_COMMAND}
      -DDATABASE_DIRECTORY=${FELIDAE_ATOMIC_DELETE_TEST_DB}
      -P ${CMAKE_SOURCE_DIR}/cmake/CleanFelidaeTestDatabase.cmake)
  set_tests_properties(atomic_delete_clean PROPERTIES FIXTURES_SETUP atomic_delete_clean)

  add_test(NAME atomic_delete_rejects_referenced_row
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/atomic_delete/atomic_delete_seed_fail.fx)
  set_tests_properties(atomic_delete_rejects_referenced_row PROPERTIES
    FIXTURES_REQUIRED atomic_delete_clean
    FIXTURES_SETUP atomic_delete_seeded
    PASS_REGULAR_EXPRESSION "Cannot delete referenced fact")

  add_test(NAME atomic_delete_leaves_all_rows
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/atomic_delete/atomic_delete_read.fx)
  set_tests_properties(atomic_delete_leaves_all_rows PROPERTIES
    FIXTURES_REQUIRED atomic_delete_seeded
    PASS_REGULAR_EXPRESSION "^3")

  set(FELIDAE_PROMOTION_TEST_PROJECT
    "${CMAKE_CURRENT_BINARY_DIR}/test-projects/persistent/promotion")
  set(FELIDAE_PROMOTION_TEST_DB "${FELIDAE_PROMOTION_TEST_PROJECT}/data.db")
  add_test(NAME schemaless_promotion_clean
    COMMAND ${CMAKE_COMMAND}
      -DDATABASE_DIRECTORY=${FELIDAE_PROMOTION_TEST_DB}
      -P ${CMAKE_SOURCE_DIR}/cmake/CleanFelidaeTestDatabase.cmake)
  set_tests_properties(schemaless_promotion_clean PROPERTIES
    FIXTURES_SETUP promotion_clean)

  add_test(NAME schemaless_promotion_seed
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/promotion/schemaless_promotion_seed.fx)
  set_tests_properties(schemaless_promotion_seed PROPERTIES
    FIXTURES_REQUIRED promotion_clean
    FIXTURES_SETUP promotion_seeded
    PASS_REGULAR_EXPRESSION "1")

  add_test(NAME schemaless_promotion_class
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/promotion/schemaless_promotion_class.fx)
  set_tests_properties(schemaless_promotion_class PROPERTIES
    FIXTURES_REQUIRED promotion_seeded
    FIXTURES_SETUP promotion_declared
    PASS_REGULAR_EXPRESSION "count: 1.*doubled: 42")


  add_test(NAME schemaless_promotion_key_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/promotion/invalid_schemaless_promotion_key.fx)
  set_tests_properties(schemaless_promotion_key_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME schemaless_promotion_type_rejected
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/promotion/invalid_schemaless_promotion_type.fx)
  set_tests_properties(schemaless_promotion_type_rejected PROPERTIES WILL_FAIL TRUE)

  add_test(NAME schemaless_promotion_same_module
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/promotion/schemaless_promotion_same_module.fx)
  set_tests_properties(schemaless_promotion_same_module PROPERTIES
    PASS_REGULAR_EXPRESSION "count: 1.*doubled: 20")

  set(FELIDAE_INDEX_TEST_PROJECT
    "${CMAKE_CURRENT_BINARY_DIR}/test-projects/persistent/index")
  set(FELIDAE_INDEX_TEST_DB "${FELIDAE_INDEX_TEST_PROJECT}/data.db")
  add_test(NAME rocks_index_clean
    COMMAND ${CMAKE_COMMAND}
      -DDATABASE_DIRECTORY=${FELIDAE_INDEX_TEST_DB}
      -P ${CMAKE_SOURCE_DIR}/cmake/CleanFelidaeTestDatabase.cmake)
  set_tests_properties(rocks_index_clean PROPERTIES FIXTURES_SETUP index_clean)

  add_test(NAME rocks_index_seed
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/index/rocks_index_seed.fx)
  set_tests_properties(rocks_index_seed PROPERTIES
    FIXTURES_REQUIRED index_clean
    FIXTURES_SETUP index_seeded
    PASS_REGULAR_EXPRESSION "2")

  add_test(NAME rocks_index_update
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/index/rocks_index_update.fx)
  set_tests_properties(rocks_index_update PROPERTIES
    FIXTURES_REQUIRED index_seeded
    FIXTURES_SETUP index_updated
    PASS_REGULAR_EXPRESSION "changed: 1.*active: 1")

  set(FELIDAE_DESIGNATION_TEST_PROJECT
    "${CMAKE_CURRENT_BINARY_DIR}/test-projects/persistent/designation")
  set(FELIDAE_DESIGNATION_TEST_DB "${FELIDAE_DESIGNATION_TEST_PROJECT}/data.db")
  add_test(NAME rocks_designation_clean
    COMMAND ${CMAKE_COMMAND}
      -DDATABASE_DIRECTORY=${FELIDAE_DESIGNATION_TEST_DB}
      -P ${CMAKE_SOURCE_DIR}/cmake/CleanFelidaeTestDatabase.cmake)
  set_tests_properties(rocks_designation_clean PROPERTIES
    FIXTURES_SETUP designation_clean)

  add_test(NAME rocks_designation_seed
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/designation/rocks_designation_seed.fx)
  set_tests_properties(rocks_designation_seed PROPERTIES
    FIXTURES_REQUIRED designation_clean
    FIXTURES_SETUP designation_seeded
    PASS_REGULAR_EXPRESSION "1")

  add_test(NAME rocks_designation_read
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/designation/rocks_designation_read.fx)
  set_tests_properties(rocks_designation_read PROPERTIES
    FIXTURES_REQUIRED designation_seeded
    PASS_REGULAR_EXPRESSION "1")

  add_test(NAME rocks_index_restart_read
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/index/rocks_index_read.fx)
  set_tests_properties(rocks_index_restart_read PROPERTIES
    FIXTURES_REQUIRED index_updated
    PASS_REGULAR_EXPRESSION "active: 1.*inactive: 1")

  set(FELIDAE_INHERITANCE_TEST_PROJECT
    "${CMAKE_CURRENT_BINARY_DIR}/test-projects/persistent/inheritance")
  set(FELIDAE_INHERITANCE_TEST_DB "${FELIDAE_INHERITANCE_TEST_PROJECT}/data.db")
  add_test(NAME rocks_inheritance_clean
    COMMAND ${CMAKE_COMMAND}
      -DDATABASE_DIRECTORY=${FELIDAE_INHERITANCE_TEST_DB}
      -P ${CMAKE_SOURCE_DIR}/cmake/CleanFelidaeTestDatabase.cmake)
  set_tests_properties(rocks_inheritance_clean PROPERTIES FIXTURES_SETUP inheritance_clean)

  add_test(NAME rocks_inheritance_seed
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/inheritance/rocks_inheritance_seed.fx)
  set_tests_properties(rocks_inheritance_seed PROPERTIES
    FIXTURES_REQUIRED inheritance_clean
    FIXTURES_SETUP inheritance_seeded
    PASS_REGULAR_EXPRESSION "2")

  add_test(NAME rocks_inheritance_restart_read
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/inheritance/rocks_inheritance_read.fx)
  set_tests_properties(rocks_inheritance_restart_read PROPERTIES
    FIXTURES_REQUIRED inheritance_seeded
    PASS_REGULAR_EXPRESSION "2")

  set(FELIDAE_CONFIG_TEST_PROJECT
    "${CMAKE_CURRENT_BINARY_DIR}/test-projects/persistent/config")
  set(FELIDAE_CONFIG_TEST_DB "${FELIDAE_CONFIG_TEST_PROJECT}/data.db")
  add_test(NAME rocks_config_clean
    COMMAND ${CMAKE_COMMAND}
      -DDATABASE_DIRECTORY=${FELIDAE_CONFIG_TEST_DB}
      -P ${CMAKE_SOURCE_DIR}/cmake/CleanFelidaeTestDatabase.cmake)
  set_tests_properties(rocks_config_clean PROPERTIES FIXTURES_SETUP config_clean)

  add_test(NAME rocks_config_set
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/config/rocks_config_set.fx)
  set_tests_properties(rocks_config_set PROPERTIES
    FIXTURES_REQUIRED config_clean
    FIXTURES_SETUP config_written
    PASS_REGULAR_EXPRESSION "max_background_jobs: 3.*bytes_per_sync: 1048576.*fact_writes: 1")

  add_test(NAME rocks_config_restart_read
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/tests/config/rocks_config_read.fx)
  set_tests_properties(rocks_config_restart_read PROPERTIES
    FIXTURES_REQUIRED config_written
    PASS_REGULAR_EXPRESSION "max_background_jobs: 3.*bytes_per_sync: 1048576")


  add_test(NAME analytical_query_language_tour
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/v2_examples/felidae_language_tour.fx)
  add_test(NAME analytical_indexed_fact_query
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/v2_examples/selective_fact_query.fx)
  add_test(NAME expert_system_fuzzy_logic
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/v2_examples/fuzzy_logic_basics.fx)
  add_test(NAME expert_system_ancestry_reasoning
    COMMAND ${FELIDAE_FX_TEST_COMMAND} ${CMAKE_SOURCE_DIR}/v2_examples/ancestor_relatedness_analysis.fx)
  set_tests_properties(
    analytical_query_language_tour
    analytical_indexed_fact_query
    expert_system_fuzzy_logic
    expert_system_ancestry_reasoning
    PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
