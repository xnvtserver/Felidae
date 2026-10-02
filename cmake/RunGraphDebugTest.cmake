if(NOT DEFINED FELIDAE_EXECUTABLE OR NOT DEFINED FELIDAE_SOURCE OR
   NOT DEFINED FELIDAE_COMMANDS)
    message(FATAL_ERROR "Graph debugger test arguments are incomplete")
endif()

execute_process(
    COMMAND "${FELIDAE_EXECUTABLE}" "${FELIDAE_SOURCE}" --debug
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

execute_process(
    COMMAND "${FELIDAE_EXECUTABLE}" "${FELIDAE_SOURCE}"
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
