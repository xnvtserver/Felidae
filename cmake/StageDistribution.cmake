if(NOT DEFINED FELIDAE_DIST_DIR OR NOT DEFINED FELIDAE_DIST_BUILD_DIR OR NOT DEFINED FELIDAE_DIST_MODEL_DIR OR NOT DEFINED FELIDAE_DIST_VERSION)
    message(FATAL_ERROR "distribution staging paths are incomplete")
endif()

# This directory is generated only by the explicit felidae_dist target. It is
# never a build tree and is safe to replace wholesale after successful builds.
file(REMOVE_RECURSE "${FELIDAE_DIST_DIR}")
file(MAKE_DIRECTORY "${FELIDAE_DIST_DIR}")
file(MAKE_DIRECTORY "${FELIDAE_DIST_DIR}/models")

foreach(executable felidae_compiler felidae_vm)
    set(source "${FELIDAE_DIST_BUILD_DIR}/${executable}.exe")
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "distribution executable is unavailable: ${source}")
    endif()
    file(COPY_FILE "${source}" "${FELIDAE_DIST_DIR}/${executable}.exe" ONLY_IF_DIFFERENT)
endforeach()

if(NOT EXISTS "${FELIDAE_DIST_MODEL_DIR}/felidae.model")
    message(FATAL_ERROR "fixed SentencePiece model is unavailable")
endif()
file(COPY "${FELIDAE_DIST_MODEL_DIR}/" DESTINATION "${FELIDAE_DIST_DIR}/models"
     FILES_MATCHING PATTERN "*.model" PATTERN "*.pt" PATTERN "*.txt")

# Windows resolves DLLs beside the executable. These are the native C++
# LibTorch dependencies reported by dumpbin for c10/torch_cpu; deliberately
# exclude torch_python, shm and distributed-only helper DLLs.
if(DEFINED FELIDAE_DIST_TORCH_LIB_DIR AND EXISTS "${FELIDAE_DIST_TORCH_LIB_DIR}")
    foreach(runtime_dll c10.dll torch_cpu.dll libiomp5md.dll uv.dll)
        set(source_dll "${FELIDAE_DIST_TORCH_LIB_DIR}/${runtime_dll}")
        if(NOT EXISTS "${source_dll}")
            message(FATAL_ERROR "required LibTorch runtime DLL is unavailable: ${source_dll}")
        endif()
        file(COPY_FILE "${source_dll}" "${FELIDAE_DIST_DIR}/${runtime_dll}" ONLY_IF_DIFFERENT)
    endforeach()
endif()

file(WRITE "${FELIDAE_DIST_DIR}/README.txt"
"Felidae ${FELIDAE_DIST_VERSION} beta portable distribution\n\nRun from this directory:\n  felidae_compiler.exe program.fx\n  felidae_vm.exe program.bin\n\nThe compiler writes .bin next to itself; use a disposable working copy or move the artifact after compilation.\nFELBIN v8 is incompatible with legacy FELIR/.fir artifacts; recompile the .fx source.\nmodels/felidae.model is required for SentencePiece encoding/decoding.\nLibTorch DLLs are staged beside the executables when this distribution was built with runtime SSM support.\nVerify shipped files with SHA256SUMS.txt before publishing.\n")

set(release_files felidae_compiler.exe felidae_vm.exe README.txt)
if(DEFINED FELIDAE_DIST_TORCH_LIB_DIR AND EXISTS "${FELIDAE_DIST_TORCH_LIB_DIR}")
    list(APPEND release_files c10.dll torch_cpu.dll libiomp5md.dll uv.dll)
endif()
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
