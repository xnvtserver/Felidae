if(NOT DEFINED FELIDAE_EXE OR NOT DEFINED TEST_DIRECTORY)
  message(FATAL_ERROR "FELIDAE_EXE and TEST_DIRECTORY are required")
endif()

# felidae PROGRAM.fx --stdin: the program text comes from stdin and PROGRAM.fx
# is only its logical name (project directory with init.fx, import base,
# diagnostics). It need not exist, and a real file of that name is not read.
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
file(WRITE "${TEST_DIRECTORY}/init.fx"
  "import \"db\".\n"
  "db.location(\"./data.db\").\n")
file(WRITE "${TEST_DIRECTORY}/helper.fx"
  "def helper() =>\n"
  "    7.\n"
  "end\n")
file(WRITE "${TEST_DIRECTORY}/on_disk.fx"
  "def twice(value: number) =>\n"
  "    value * 100.\n"
  "end\n")

# run_stdin(<variable> <stdin text> <arguments...>) sets <variable>_output,
# <variable>_error and <variable>_result.
function(run_stdin name stdin_text)
  file(WRITE "${TEST_DIRECTORY}/${name}.stdin" "${stdin_text}")
  execute_process(
    COMMAND "${FELIDAE_EXE}" ${ARGN}
    WORKING_DIRECTORY "${TEST_DIRECTORY}"
    INPUT_FILE "${TEST_DIRECTORY}/${name}.stdin"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE result)
  set(${name}_output "${output}" PARENT_SCOPE)
  set(${name}_error "${error}" PARENT_SCOPE)
  set(${name}_result "${result}" PARENT_SCOPE)
endfunction()

# 1. A function declared on stdin, evaluated with --query; the logical file does not exist.
run_stdin(query "def twice(value: number) =>\n    value * 2.\nend\n"
  notebook.fxnb.fx --stdin --query "twice(value: 21)." )
if(NOT query_result EQUAL 0 OR NOT query_output MATCHES "42")
  message(FATAL_ERROR "stdin program with --query failed (${query_result}):\n${query_output}\n${query_error}")
endif()
if(EXISTS "${TEST_DIRECTORY}/notebook.fxnb.fx")
  message(FATAL_ERROR "--stdin must not create the logical file")
endif()

# 2. Without --query the program's main runs.
run_stdin(entry "def main() =>\n    print(value: \"from stdin\").\nend\n"
  notebook.fxnb.fx --stdin)
if(NOT entry_result EQUAL 0 OR NOT entry_output MATCHES "from stdin")
  message(FATAL_ERROR "stdin program did not run main (${entry_result}):\n${entry_output}\n${entry_error}")
endif()

# 3. stdin wins over a real file of the same name.
run_stdin(shadow "def twice(value: number) =>\n    value * 2.\nend\n"
  on_disk.fx --stdin --query "twice(value: 21).")
if(NOT shadow_result EQUAL 0 OR NOT shadow_output MATCHES "42" OR shadow_output MATCHES "2100")
  message(FATAL_ERROR "--stdin read the file on disk instead of stdin:\n${shadow_output}\n${shadow_error}")
endif()

# 4. Imports resolve from the logical path's directory.
run_stdin(imports "import \"helper.fx\".\n"
  notebook.fxnb.fx --stdin --query "helper().")
if(NOT imports_result EQUAL 0 OR NOT imports_output MATCHES "7")
  message(FATAL_ERROR "import from the logical path's folder failed (${imports_result}):\n${imports_output}\n${imports_error}")
endif()

# 5. A syntax error names the logical file and a position, and fails.
run_stdin(broken "def broken( =>\nend\n"
  notebook.fxnb.fx --stdin --query "broken().")
if(broken_result EQUAL 0 OR NOT broken_error MATCHES "notebook.fxnb.fx" OR NOT broken_error MATCHES "line [0-9]+")
  message(FATAL_ERROR "syntax error was not reported with file and line (${broken_result}):\n${broken_error}")
endif()

# 6. --stdin cannot be combined with --debug, which uses stdin for commands.
run_stdin(debug "" notebook.fxnb.fx --stdin --debug)
if(debug_result EQUAL 0 OR NOT debug_error MATCHES "cannot be combined with --debug")
  message(FATAL_ERROR "--stdin with --debug was not rejected (${debug_result}):\n${debug_error}")
endif()

# 7. --stdin needs a file argument.
run_stdin(nofile "" --stdin)
if(nofile_result EQUAL 0)
  message(FATAL_ERROR "--stdin without a program file was accepted:\n${nofile_output}")
endif()
