if(NOT DEFINED FELIDAE_EXECUTABLE OR NOT DEFINED TEST_DIRECTORY)
  message(FATAL_ERROR "Project configuration test arguments are incomplete")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
foreach(project missing empty valid)
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

execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" "${TEST_DIRECTORY}/valid/main.fx"
  RESULT_VARIABLE valid_result)
if(NOT valid_result EQUAL 0)
  message(FATAL_ERROR "Valid init.fx did not execute")
endif()

execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" --db "${TEST_DIRECTORY}/legacy.db"
          "${TEST_DIRECTORY}/valid/main.fx"
  RESULT_VARIABLE legacy_result
  ERROR_VARIABLE legacy_error)
if(legacy_result EQUAL 0 OR NOT legacy_error MATCHES "Unknown option: --db")
  message(FATAL_ERROR "Legacy --db syntax was not rejected: ${legacy_error}")
endif()

execute_process(
  COMMAND "${FELIDAE_EXECUTABLE}" db stop "${TEST_DIRECTORY}/valid"
  OUTPUT_QUIET ERROR_QUIET)
