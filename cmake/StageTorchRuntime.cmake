if(NOT DEFINED FELIDAE_EXECUTABLE OR NOT DEFINED FELIDAE_TORCH_LIB_DIR OR
   NOT DEFINED FELIDAE_OUTPUT_DIR OR NOT DEFINED FELIDAE_RUNTIME_MANIFEST)
    message(FATAL_ERROR "LibTorch runtime staging arguments are incomplete")
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${FELIDAE_EXECUTABLE}"
    DIRECTORIES "${FELIDAE_TORCH_LIB_DIR}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*")

set(staged_names)
foreach(dependency IN LISTS resolved)
    get_filename_component(torch_root "${FELIDAE_TORCH_LIB_DIR}" REALPATH)
    get_filename_component(dependency_path "${dependency}" REALPATH)
    string(FIND "${dependency_path}" "${torch_root}/" torch_prefix)
    if(torch_prefix EQUAL 0)
        get_filename_component(dependency_name "${dependency}" NAME)
        file(COPY_FILE "${dependency}" "${FELIDAE_OUTPUT_DIR}/${dependency_name}" ONLY_IF_DIFFERENT)
        list(APPEND staged_names "${dependency_name}")
    endif()
endforeach()

if(unresolved)
    message(FATAL_ERROR "unresolved LibTorch runtime dependencies: ${unresolved}")
endif()
list(REMOVE_DUPLICATES staged_names)
list(SORT staged_names)
set(manifest_text "")
foreach(dependency_name IN LISTS staged_names)
    string(APPEND manifest_text "${dependency_name}\n")
endforeach()
file(WRITE "${FELIDAE_RUNTIME_MANIFEST}" "${manifest_text}")
