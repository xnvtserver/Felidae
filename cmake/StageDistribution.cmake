if(NOT DEFINED FELIDAE_DIST_DIR OR NOT DEFINED FELIDAE_DIST_BUILD_DIR OR
   NOT DEFINED FELIDAE_DIST_CONFIG OR
   NOT DEFINED FELIDAE_DIST_EXECUTABLE_SUFFIX OR
   NOT DEFINED FELIDAE_DIST_MODEL_DIR OR NOT DEFINED FELIDAE_DIST_VERSION OR
   NOT DEFINED FELIDAE_DIST_HAS_LIBTORCH)
    message(FATAL_ERROR "distribution staging paths are incomplete")
endif()

if(NOT FELIDAE_DIST_CONFIG STREQUAL "Release")
    message(FATAL_ERROR
        "felidae_dist requires a Release configuration; received '${FELIDAE_DIST_CONFIG}'")
endif()

# This directory is generated only by the explicit felidae_dist target inside
# the active build tree and is safe to replace wholesale after successful builds.
file(REMOVE_RECURSE "${FELIDAE_DIST_DIR}")
file(MAKE_DIRECTORY "${FELIDAE_DIST_DIR}")
file(MAKE_DIRECTORY "${FELIDAE_DIST_DIR}/models")

foreach(executable felidae_compiler felidae_vm)
    set(executable_name "${executable}${FELIDAE_DIST_EXECUTABLE_SUFFIX}")
    set(source "${FELIDAE_DIST_BUILD_DIR}/${executable_name}")
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "distribution executable is unavailable: ${source}")
    endif()
    file(COPY_FILE "${source}" "${FELIDAE_DIST_DIR}/${executable_name}" ONLY_IF_DIFFERENT)
endforeach()

if(NOT EXISTS "${FELIDAE_DIST_MODEL_DIR}/felidae.model")
    message(FATAL_ERROR "fixed SentencePiece model is unavailable")
endif()
file(COPY "${FELIDAE_DIST_MODEL_DIR}/" DESTINATION "${FELIDAE_DIST_DIR}/models"
     FILES_MATCHING PATTERN "*.model" PATTERN "*.pt" PATTERN "*.txt")

# Post-build runtime discovery records the exact dependency closure for each
# executable. Never glob the build directory: it may also contain unrelated
# plugins or stale DLLs that are not part of the portable runtime contract.
set(runtime_names)
if(FELIDAE_DIST_HAS_LIBTORCH)
    foreach(executable felidae_compiler felidae_vm)
        set(runtime_manifest
            "${FELIDAE_DIST_BUILD_DIR}/felidae-torch-runtime-${executable}.txt")
        if(NOT EXISTS "${runtime_manifest}")
            message(FATAL_ERROR "LibTorch runtime manifest is unavailable: ${runtime_manifest}")
        endif()
        file(STRINGS "${runtime_manifest}" executable_runtime_names)
        list(APPEND runtime_names ${executable_runtime_names})
    endforeach()
    list(REMOVE_DUPLICATES runtime_names)
    list(SORT runtime_names)
    foreach(runtime_name IN LISTS runtime_names)
        if(NOT runtime_name MATCHES "^[A-Za-z0-9_.+-]+\\.dll$")
            message(FATAL_ERROR "invalid LibTorch runtime manifest entry: ${runtime_name}")
        endif()
        set(source_dll "${FELIDAE_DIST_BUILD_DIR}/${runtime_name}")
        if(NOT EXISTS "${source_dll}")
            message(FATAL_ERROR "resolved LibTorch runtime DLL is unavailable: ${source_dll}")
        endif()
        file(COPY_FILE "${source_dll}" "${FELIDAE_DIST_DIR}/${runtime_name}" ONLY_IF_DIFFERENT)
    endforeach()
endif()

file(WRITE "${FELIDAE_DIST_DIR}/README.txt"
"Felidae ${FELIDAE_DIST_VERSION} beta portable distribution\n\nRun from this directory:\n  felidae_compiler${FELIDAE_DIST_EXECUTABLE_SUFFIX} program.fx\n  felidae_vm${FELIDAE_DIST_EXECUTABLE_SUFFIX} program.bin\n\nThe compiler writes .bin next to itself; use a disposable working copy or move the artifact after compilation.\nFELBIR v16 is the current beta executable IR; recompile source after beta format changes.\nmodels/felidae.model is required for SentencePiece encoding and symbol decoding.\nLibTorch DLLs are staged beside the executables when this distribution was built with runtime SSM support.\nVerify shipped files with SHA256SUMS.txt before publishing.\n")

set(release_files
    "felidae_compiler${FELIDAE_DIST_EXECUTABLE_SUFFIX}"
    "felidae_vm${FELIDAE_DIST_EXECUTABLE_SUFFIX}"
    README.txt)
foreach(runtime_name IN LISTS runtime_names)
    list(APPEND release_files "${runtime_name}")
endforeach()
# All copied model artifacts, including nested GRU checkpoints and manifests,
# are part of the release integrity boundary rather than an unchecked sidecar.
file(GLOB_RECURSE staged_model_entries RELATIVE "${FELIDAE_DIST_DIR}"
     "${FELIDAE_DIST_DIR}/models/*")
foreach(entry IN LISTS staged_model_entries)
    if(NOT IS_DIRECTORY "${FELIDAE_DIST_DIR}/${entry}")
        list(APPEND release_files "${entry}")
    endif()
endforeach()
list(REMOVE_DUPLICATES release_files)
list(SORT release_files)
set(checksums "# Felidae ${FELIDAE_DIST_VERSION} beta distribution SHA-256\n")
foreach(release_file IN LISTS release_files)
    file(SHA256 "${FELIDAE_DIST_DIR}/${release_file}" digest)
    string(APPEND checksums "${digest}  ${release_file}\n")
endforeach()
file(WRITE "${FELIDAE_DIST_DIR}/SHA256SUMS.txt" "${checksums}")
