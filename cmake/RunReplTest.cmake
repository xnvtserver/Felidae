if(NOT DEFINED FELIDAE_EXE OR NOT DEFINED TEST_DIRECTORY)
  message(FATAL_ERROR "FELIDAE_EXE and TEST_DIRECTORY are required")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
file(WRITE "${TEST_DIRECTORY}/init.fx"
  "import \"db\".\n"
  "db.location(\"./data.db\").\n")
set(input_file "${TEST_DIRECTORY}/repl-session.txt")
file(WRITE "${input_file}"
  "def broken( =>\n"
  "end\n"
  "# comment-only REPL input is valid trivia\n"
  "answer := 7.\n"
  ":clear\n"
  "answer\n"
  "import \"math\".\n"
  "math.sqrt(value: 9)\n"
  "Probe(id: \"probe-1\").\n"
  "? Probe(id: x).\n"
  "class ReplNode\n"
  "    key(id).\n"
  "    id: string.\n"
  "end\n"
  "ReplNode(id: \"node-1\").\n"
  "? ReplNode(id: node_id).\n"
  "def twice(value: number) =>\n"
  "    return value * 2.\n"
  "end\n"
  "twice(21)\n"
  ":metrics\n"
  ":debug on\n"
  "twice(2)\n"
  ":debug off\n"
  "def hello_world() =>\n"
  "    print(value: \"something\").\n"
  "end\n"
  "hello_world()\n"
  "class ReplEmployee\n"
  "    key(id).\n"
  "    id: string.\n"
  "end\n"
  "class ReplDepartment\n"
  "    key(id).\n"
  "    id: string.\n"
  "end\n"
  "ReplEmployee(id: \"employee-1\").\n"
  "ReplDepartment(id: \"department-1\").\n"
  "Link(\n"
  "    from: ReplEmployee(id: \"employee-1\"),\n"
  "    to: ReplDepartment(id: \"department-1\"),\n"
  "    properties: {kind: \"works_in\", since: 2024, confidence: 0.9}\n"
  ").\n"
  "ReplEmployee.where(id: \"employee-1\").join(properties: {kind: \"works_in\"}, direction: forward.class).count()\n"
  "def choose(value: number) =>\n"
  "    # end in a comment is not a block boundary\n"
  "    if value > 0 then\n"
  "        value > 0.\n"
  "    end\n"
  "    return \"end\".\n"
  "end\n"
  "choose(1)\n"
  ":quit\n")

execute_process(
  COMMAND "${FELIDAE_EXE}"
  WORKING_DIRECTORY "${TEST_DIRECTORY}"
  INPUT_FILE "${input_file}"
  OUTPUT_VARIABLE repl_output
  ERROR_VARIABLE repl_error
  RESULT_VARIABLE repl_result)

if(NOT repl_result EQUAL 0)
  message(FATAL_ERROR "REPL failed (${repl_result}): ${repl_error}\n${repl_output}")
endif()
if(NOT repl_output MATCHES "\\[error\\]")
  message(FATAL_ERROR "REPL did not diagnose malformed multiline source:\n${repl_output}")
endif()
if(repl_output MATCHES "comment-only.*\\[error\\]")
  message(FATAL_ERROR "REPL treated a comment as an expression error:\n${repl_output}")
endif()
if(NOT repl_output MATCHES "=> 7" OR
   NOT repl_output MATCHES "=> 3" OR
   NOT repl_output MATCHES "x = \"probe-1\"" OR
   NOT repl_output MATCHES "node_id = \"node-1\"")
  message(FATAL_ERROR "REPL did not execute standard global/fact/query source forms:\n${repl_output}")
endif()
if(NOT repl_output MATCHES "\\[screen cleared\\]")
  message(FATAL_ERROR "REPL :clear command did not execute:\n${repl_output}")
endif()
if(NOT repl_output MATCHES "\\[ok\\] declaration installed[^\n\r]*[\n\r]+=> 42")
  message(FATAL_ERROR "REPL did not install and call multiline source:\n${repl_output}")
endif()
if(NOT repl_output MATCHES "something[\n\r]+=> fn:tuple\\(value: true\\)")
  message(FATAL_ERROR "REPL did not execute bare print or report its successful truth tuple:\n${repl_output}")
endif()
if(NOT repl_output MATCHES "=> 1")
  message(FATAL_ERROR "REPL did not wait for and execute the complete multiline Link declaration:\n${repl_output}")
endif()
if(NOT repl_output MATCHES "=> \"end\"")
  message(FATAL_ERROR "REPL did not track nested or quoted end tokens:\n${repl_output}")
endif()
if(NOT repl_output MATCHES "Last REPL action" OR
   NOT repl_output MATCHES "RocksDB state")
  message(FATAL_ERROR "REPL did not report execution and RocksDB statistics:\n${repl_output}")
endif()
if(NOT repl_output MATCHES "\\[debug\\].*return")
  message(FATAL_ERROR "REPL goal-hook debugger did not trace the real method body:\n${repl_output}")
endif()
