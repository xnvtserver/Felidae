if(NOT DEFINED FELIDAE_EXECUTABLE OR NOT DEFINED TEST_DIRECTORY)
  message(FATAL_ERROR "Project configuration test arguments are incomplete")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
foreach(project missing empty valid parent nested_a nested_b no_import
                no_location duplicate_location invalid_statement duplicate_option)
  file(MAKE_DIRECTORY "${TEST_DIRECTORY}/${project}")
  file(WRITE "${TEST_DIRECTORY}/${project}/main.fx"
    "def main() =>\n"
    "    return 1.\n"
    "end\n")
endforeach()
file(WRITE "${TEST_DIRECTORY}/empty/init.fx" "# comments are not configuration\n")
file(WRITE "${TEST_DIRECTORY}/valid/init.fx"
  "import \"db\".\n"
  "db.location(\"./data.db\").\n"
  "db.configure(options: {max_background_jobs: 2, bytes_per_sync: 4096}).\n")
file(WRITE "${TEST_DIRECTORY}/valid/main.fx"
  "def main() =>\n"
  "    return db.config().max_background_jobs.\n"
  "end\n")
file(WRITE "${TEST_DIRECTORY}/no_import/init.fx"
  "db.location(\"./data.db\").\n")
file(WRITE "${TEST_DIRECTORY}/no_location/init.fx"
  "import \"db\".\n")
file(WRITE "${TEST_DIRECTORY}/duplicate_location/init.fx"
  "import \"db\".\n"
  "db.location(\"./one.db\").\n"
  "db.location(\"./two.db\").\n")
file(WRITE "${TEST_DIRECTORY}/invalid_statement/init.fx"
  "import \"db\".\n"
  "db.location(\"./data.db\").\n"
  "value := 1.\n")
file(WRITE "${TEST_DIRECTORY}/duplicate_option/init.fx"
  "import \"db\".\n"
  "db.location(\"./data.db\").\n"
  "db.configure(options: {max_background_jobs: 2}).\n"
  "db.configure(options: {max_background_jobs: 3}).\n")
file(WRITE "${TEST_DIRECTORY}/parent/init.fx"
  "import \"db\".\n"
  "db.location(\"./data.db\").\n")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}/parent/child")
file(WRITE "${TEST_DIRECTORY}/parent/child/main.fx"
  "def main() =>\n"
  "    return 1.\n"
  "end\n")
foreach(project nested_a nested_b)
  file(WRITE "${TEST_DIRECTORY}/${project}/init.fx"
    "import \"db\".\n"
    "db.location(\"../shared.db\").\n")
endforeach()
file(WRITE "${TEST_DIRECTORY}/nested_a/main.fx"
  "def main() =>\n"
  "    return 11.\n"
  "end\n")
file(WRITE "${TEST_DIRECTORY}/nested_b/main.fx"
  "def main() =>\n"
  "    return 22.\n"
  "end\n")

execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" "${TEST_DIRECTORY}/missing/main.fx"
  RESULT_VARIABLE missing_result
  ERROR_VARIABLE missing_error)
if(missing_result EQUAL 0 OR NOT missing_error MATCHES "requires init[.]fx")
  message(FATAL_ERROR "Missing init.fx was not rejected clearly: ${missing_error}")
endif()

execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" "${TEST_DIRECTORY}/empty/main.fx"
  RESULT_VARIABLE empty_result
  ERROR_VARIABLE empty_error)
if(empty_result EQUAL 0 OR NOT empty_error MATCHES "init[.]fx is empty")
  message(FATAL_ERROR "Empty init.fx was not rejected clearly: ${empty_error}")
endif()

function(expect_project_error project pattern)
  execute_process(
    COMMAND "${FELIDAE_EXECUTABLE}" "${TEST_DIRECTORY}/${project}/main.fx"
    RESULT_VARIABLE project_result
    ERROR_VARIABLE project_error)
  if(project_result EQUAL 0 OR NOT project_error MATCHES "${pattern}")
    message(FATAL_ERROR
      "Invalid project '${project}' was not rejected clearly: ${project_error}")
  endif()
endfunction()

expect_project_error(no_import "must contain import")
expect_project_error(no_location "must declare db[.]location")
expect_project_error(duplicate_location "exactly once")
expect_project_error(invalid_statement "only imports and database configuration")
expect_project_error(duplicate_option "unique option names")

execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" "${TEST_DIRECTORY}/valid/main.fx"
  RESULT_VARIABLE valid_result
  OUTPUT_VARIABLE valid_output)
if(NOT valid_result EQUAL 0 OR NOT valid_output MATCHES "2")
  message(FATAL_ERROR
    "Valid init.fx did not apply its RocksDB configuration: ${valid_output}")
endif()

execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" --db "${TEST_DIRECTORY}/legacy.db"
          "${TEST_DIRECTORY}/valid/main.fx"
  RESULT_VARIABLE legacy_result
  ERROR_VARIABLE legacy_error)
if(legacy_result EQUAL 0 OR NOT legacy_error MATCHES "Unknown option: --db")
  message(FATAL_ERROR "Legacy --db syntax was not rejected: ${legacy_error}")
endif()

# Project lookup is intentionally exact. A parent init.fx must not silently
# configure a directly executed child program.
execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" "${TEST_DIRECTORY}/parent/child/main.fx"
  RESULT_VARIABLE child_result
  ERROR_VARIABLE child_error)
if(child_result EQUAL 0 OR NOT child_error MATCHES "requires init[.]fx")
  message(FATAL_ERROR "A parent init.fx was incorrectly inherited: ${child_error}")
endif()

# A relative program path must select its own sibling init.fx regardless of
# which project directory a previous run used.
execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" main.fx
  WORKING_DIRECTORY "${TEST_DIRECTORY}/nested_a"
  RESULT_VARIABLE nested_a_result
  OUTPUT_VARIABLE nested_a_output)
execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" main.fx
  WORKING_DIRECTORY "${TEST_DIRECTORY}/nested_b"
  RESULT_VARIABLE nested_b_result
  OUTPUT_VARIABLE nested_b_output)
if(NOT nested_a_result EQUAL 0 OR NOT nested_a_output MATCHES "11")
  message(FATAL_ERROR "First relative project execution failed: ${nested_a_output}")
endif()
if(NOT nested_b_result EQUAL 0 OR NOT nested_b_output MATCHES "22")
  message(FATAL_ERROR
    "Relative project execution did not use its sibling init.fx: ${nested_b_output}")
endif()
