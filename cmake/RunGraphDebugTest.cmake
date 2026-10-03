if(NOT DEFINED FELIDAE_EXECUTABLE OR NOT DEFINED FELIDAE_SOURCE OR
   NOT DEFINED FELIDAE_COMMANDS OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "Graph debugger test arguments are incomplete")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(TEST_DIRECTORY "${TEST_DIRECTORY}/${run_id}")
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
get_filename_component(source_name "${FELIDAE_SOURCE}" NAME)
file(COPY "${FELIDAE_SOURCE}" DESTINATION "${TEST_DIRECTORY}")
file(WRITE "${TEST_DIRECTORY}/init.fx"
    "import \"db\".\n"
    "db.location(\"./data.db\").\n")
set(project_source "${TEST_DIRECTORY}/${source_name}")

execute_process(
    COMMAND "${FELIDAE_EXECUTABLE}" "${project_source}" --debug
    INPUT_FILE "${FELIDAE_COMMANDS}"
    RESULT_VARIABLE debug_result
    OUTPUT_VARIABLE debug_output
    ERROR_VARIABLE debug_error)
if(NOT debug_result EQUAL 0)
    message(FATAL_ERROR "Graph debugger failed: ${debug_error}")
endif()
set(debug_transcript "${debug_output}\n${debug_error}")
foreach(required
        "FELIDAE_DEBUG_STOPPED"
        "FELIDAE_DEBUG_LOCALS_BEGIN"
        "Graph"
        "Employee#"
        "Department#"
        "class_edges: 1"
        "links: 1"
        "FELIDAE_DEBUG_TERMINATED")
    string(FIND "${debug_transcript}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "Graph debugger transcript is missing '${required}':\n${debug_transcript}")
    endif()
endforeach()

# The normal-run half starts from the same clean graph state as the debug
# half. Link insertion is intentionally non-idempotent.
file(REMOVE_RECURSE "${TEST_DIRECTORY}/data.db")

execute_process(
    COMMAND "${FELIDAE_EXECUTABLE}" "${project_source}"
    RESULT_VARIABLE normal_result
    OUTPUT_VARIABLE normal_output
    ERROR_VARIABLE normal_error)
if(NOT normal_result EQUAL 0)
    message(FATAL_ERROR "Normal graph execution failed: ${normal_error}")
endif()
set(normal_transcript "${normal_output}\n${normal_error}")
string(FIND "${normal_transcript}" "FELIDAE_DEBUG_" debug_marker)
if(NOT debug_marker EQUAL -1)
    message(FATAL_ERROR
        "Normal graph execution initialized the debugger:\n${normal_transcript}")
endif()
