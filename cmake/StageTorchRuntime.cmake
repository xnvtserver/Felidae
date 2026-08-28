if(NOT DEFINED FELIDAE_EXECUTABLE OR NOT DEFINED FELIDAE_TORCH_LIB_DIR OR
   NOT DEFINED FELIDAE_OUTPUT_DIR OR NOT DEFINED FELIDAE_RUNTIME_MANIFEST)
    message(FATAL_ERROR "LibTorch runtime staging arguments are incomplete")
endif()

# Silences a CMake policy warning about path normalization during matching;
# NEW is what current CMake recommends and has no effect on which files this
# script stages.
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${FELIDAE_EXECUTABLE}"
    DIRECTORIES "${FELIDAE_TORCH_LIB_DIR}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    # api-ms-*/ext-ms-* are Windows API Set forwarders: never real files, the
    # loader resolves them internally.
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*")

# AzureAttestManager.dll, AzureAttestNormal.dll, HvsiFileTrust.dll,
# PdmUtilities.dll, and wpaxholder.dll are optional delay-loaded Windows
# security/diagnostics providers referenced by ucrtbased.dll (the Debug
# UCRT); most machines, including normal dev machines, never have them
# installed, and the OS already tolerates their absence at runtime for
# delay-loaded imports. None of them are LibTorch dependencies, so filtering
# them out here cannot hide a genuinely missing Torch DLL -- only a truly
# missing Torch DLL can still fail this script below. GET_RUNTIME_DEPENDENCIES'
# own PRE_EXCLUDE_REGEXES does not reliably suppress these (observed still
# reporting some of them as unresolved even when listed there), so they are
# filtered out of the resolved list explicitly instead.
if(unresolved)
    list(FILTER unresolved EXCLUDE REGEX
        "^(AzureAttestManager|AzureAttestNormal|HvsiFileTrust|PdmUtilities|wpaxholder)\\.dll$")
endif()

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
