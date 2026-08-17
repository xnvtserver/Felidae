if(NOT DEFINED FELIDAE_BETA_DIST_DIR OR NOT DEFINED FELIDAE_BETA_EXAMPLE)
    message(FATAL_ERROR "beta release check paths are incomplete")
endif()

foreach(required felidae_compiler.exe felidae_vm.exe models/felidae.model SHA256SUMS.txt README.txt)
    if(NOT EXISTS "${FELIDAE_BETA_DIST_DIR}/${required}")
        message(FATAL_ERROR "beta distribution is missing ${required}")
    endif()
endforeach()

foreach(forbidden CMakeCache.txt compile_commands.json felidae_compiler.pdb felidae_vm.pdb)
    if(EXISTS "${FELIDAE_BETA_DIST_DIR}/${forbidden}")
        message(FATAL_ERROR "beta distribution contains build-only artifact ${forbidden}")
    endif()
endforeach()

execute_process(COMMAND "${FELIDAE_BETA_DIST_DIR}/felidae_compiler.exe" --version
    RESULT_VARIABLE compiler_version_result OUTPUT_VARIABLE compiler_version ERROR_VARIABLE compiler_version_error)
if(NOT compiler_version_result EQUAL 0)
    message(FATAL_ERROR "beta compiler does not start: ${compiler_version_error}")
endif()
execute_process(COMMAND "${FELIDAE_BETA_DIST_DIR}/felidae_vm.exe" --version
    RESULT_VARIABLE vm_version_result OUTPUT_VARIABLE vm_version ERROR_VARIABLE vm_version_error)
if(NOT vm_version_result EQUAL 0)
    message(FATAL_ERROR "beta VM does not start: ${vm_version_error}")
endif()

get_filename_component(example_name "${FELIDAE_BETA_EXAMPLE}" NAME_WE)
set(binary "${FELIDAE_BETA_DIST_DIR}/${example_name}.fir")
execute_process(COMMAND "${FELIDAE_BETA_DIST_DIR}/felidae_compiler.exe" "${FELIDAE_BETA_EXAMPLE}"
    RESULT_VARIABLE compile_result OUTPUT_VARIABLE compile_output ERROR_VARIABLE compile_error)
if(NOT compile_result EQUAL 0 OR NOT EXISTS "${binary}")
    message(FATAL_ERROR "beta compiler smoke failed: ${compile_error}${compile_output}")
endif()
execute_process(COMMAND "${FELIDAE_BETA_DIST_DIR}/felidae_vm.exe" "${binary}"
    RESULT_VARIABLE vm_result OUTPUT_VARIABLE vm_output ERROR_VARIABLE vm_error)
file(REMOVE "${binary}")
if(NOT vm_result EQUAL 0)
    message(FATAL_ERROR "beta VM smoke failed: ${vm_error}${vm_output}")
endif()
if(NOT vm_output MATCHES "756")
    message(FATAL_ERROR "beta VM smoke output is unexpected: ${vm_output}")
endif()
message(STATUS "Felidae beta release check passed: ${compiler_version}${vm_version}")
