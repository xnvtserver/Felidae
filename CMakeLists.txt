cmake_minimum_required(VERSION 3.21)
project(felidae LANGUAGES CXX)

set(FELIDAE_VERSION "0.2.3-beta.1" CACHE STRING
    "Version recorded in staged Felidae distributions")

# Keep all generated CMake state under one ignored repository directory.
# Supported layouts include build/ itself and named configurations such as
# build/debug, build/asan, and build/release.
get_filename_component(FELIDAE_BUILD_ROOT "${CMAKE_SOURCE_DIR}/build" ABSOLUTE)
get_filename_component(FELIDAE_BINARY_DIR "${CMAKE_BINARY_DIR}" ABSOLUTE)
string(FIND "${FELIDAE_BINARY_DIR}/" "${FELIDAE_BUILD_ROOT}/"
    FELIDAE_BUILD_ROOT_POSITION)
if(NOT FELIDAE_BUILD_ROOT_POSITION EQUAL 0)
    message(FATAL_ERROR
        "Felidae build directories must be inside ${FELIDAE_BUILD_ROOT}. "
        "Use -B build/<configuration> (for example, -B build/debug).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(FELIDAE_ENABLE_SANITIZERS
    "Build compiler, register VM, and tooling with address/undefined-behavior sanitizers" OFF)
option(FELIDAE_BUILD_TESTS "Build Felidae regression tests" ON)
option(FELIDAE_ENABLE_STRICT_WARNINGS
    "Enable strict warnings for Felidae-owned targets only" OFF)
option(FELIDAE_ENABLE_MSVC_STATIC_ANALYSIS
    "Enable MSVC /analyze on compiler and Form VM targets" OFF)
option(FELIDAE_ENABLE_CCACHE "Reuse native compilation results with ccache" ON)
if(FELIDAE_ENABLE_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(FELIDAE_CCACHE_PROGRAM ccache)
    if(FELIDAE_CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER
            "${CMAKE_COMMAND};-E;env;CCACHE_DIR=${CMAKE_BINARY_DIR}/ccache;${FELIDAE_CCACHE_PROGRAM}")
    endif()
endif()
if(ANDROID)
    set(FELIDAE_LIBTORCH_DEFAULT OFF)
else()
    set(FELIDAE_LIBTORCH_DEFAULT ON)
endif()
option(FELIDAE_ENABLE_LIBTORCH
    "Enable mandatory desktop LibTorch support; disable only on targets where LibTorch is unavailable"
    ${FELIDAE_LIBTORCH_DEFAULT})
option(FELIDAE_ENABLE_TRAINING
    "Build explicit compiler and VM C++ model-training commands" OFF)

if(FELIDAE_ENABLE_TRAINING AND NOT FELIDAE_ENABLE_LIBTORCH)
    message(FATAL_ERROR
        "FELIDAE_ENABLE_TRAINING requires FELIDAE_ENABLE_LIBTORCH=ON")
endif()

# Keep the lexical model native and reproducible: SentencePiece is pinned as a
# git submodule instead of being found from an arbitrary system installation.
set(SPM_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
set(FELIDAE_HOST_PROTOC "" CACHE FILEPATH
    "Native protoc used while cross-compiling SentencePiece")
if(FELIDAE_HOST_PROTOC)
    if(NOT EXISTS "${FELIDAE_HOST_PROTOC}")
        message(FATAL_ERROR "FELIDAE_HOST_PROTOC does not exist")
    endif()
    set(SPM_PROTOC_EXECUTABLE "${FELIDAE_HOST_PROTOC}" CACHE FILEPATH "" FORCE)
elseif(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR
        "Cross-compiling requires -DFELIDAE_HOST_PROTOC=/path/to/native/protoc")
endif()
# SentencePiece otherwise fetches these sources while configuring. Use their
# pinned submodules when initialized; an archive/source-only checkout retains
# SentencePiece's normal FetchContent behavior instead of referring to a
# nonexistent local path. FetchContent preserves the dependency hyphen.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/abseil-cpp/CMakeLists.txt")
    set("FETCHCONTENT_SOURCE_DIR_ABSEIL-CPP" "${CMAKE_SOURCE_DIR}/third_party/abseil-cpp"
        CACHE PATH "Pinned Abseil source used by SentencePiece" FORCE)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/protobuf/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_PROTOBUF "${CMAKE_SOURCE_DIR}/third_party/protobuf"
        CACHE PATH "Pinned Protobuf source used by SentencePiece" FORCE)
endif()
add_subdirectory(third_party/sentencepiece EXCLUDE_FROM_ALL)
add_subdirectory(third_party/nlohmann_json EXCLUDE_FROM_ALL)

# SentencePiece's generated public headers include Protobuf headers such as
# google/protobuf/port_def.inc. Its vendored CMake build keeps the Protobuf
# dependency private, so downstream Felidae targets otherwise compile without
# the pinned Protobuf include directory. Export only the required header path
# through the two SentencePiece targets that Felidae consumes.
foreach(FELIDAE_SENTENCEPIECE_TARGET sentencepiece-static sentencepiece_train-static)
    if(TARGET ${FELIDAE_SENTENCEPIECE_TARGET})
        target_include_directories(${FELIDAE_SENTENCEPIECE_TARGET} SYSTEM INTERFACE
            "${CMAKE_SOURCE_DIR}/third_party/protobuf/src")
    endif()
endforeach()
find_package(OpenMP)
if(FELIDAE_BUILD_TESTS)
    find_package(Threads REQUIRED)
endif()

# These paths are also part of the isolated LibTorch trainer's manifest
# contract, so define them before that target is created.
set(FELIDAE_SENTENCEPIECE_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/models")
set(FELIDAE_SENTENCEPIECE_MODEL_PATH "${FELIDAE_SENTENCEPIECE_GENERATED_DIR}/felidae.model")
set(FELIDAE_SENTENCEPIECE_IDS_HEADER
    "${FELIDAE_SENTENCEPIECE_GENERATED_DIR}/FelidaeSentencePieceIds.h")
set(FELIDAE_SENTENCEPIECE_DATASET
    "${CMAKE_CURRENT_SOURCE_DIR}/datasets/tokenizer/felidae-tokenizer-v1.jsonl")

# SentencePiece owns the pinned Protobuf target used by the project. Find
# LibTorch afterwards so its bundled dependency discovery cannot create a
# second protobuf::libprotobuf target on Windows.
if(FELIDAE_ENABLE_LIBTORCH)
    find_package(Torch REQUIRED)
    # Shared LibTorch embeds the Protobuf implementation used by
    # libtorch_cpu. Its Caffe2 package normally supplies a dummy
    # protobuf::libprotobuf interface target, but SentencePiece has already
    # created that target for its pinned frontend dependency by the time
    # TorchConfig is loaded. Caffe2 then accidentally propagates Felidae's
    # full static Protobuf through torch_cpu, placing both libprotobuf and
    # libprotobuf-lite in the compiler link. Remove exactly that unintended
    # interface edge; SentencePiece keeps its own required lite dependency,
    # and LibTorch continues to use the implementation embedded in its shared
    # library.
    if(TARGET torch_cpu)
        get_target_property(FELIDAE_TORCH_CPU_LINK_LIBRARIES
            torch_cpu INTERFACE_LINK_LIBRARIES)
        if(FELIDAE_TORCH_CPU_LINK_LIBRARIES)
            # CMake/LibTorch versions may wrap this entry in $<LINK_ONLY:...>;
            # filter by target spelling rather than relying on an exact item.
            list(FILTER FELIDAE_TORCH_CPU_LINK_LIBRARIES EXCLUDE REGEX
                "protobuf::libprotobuf")
            set_target_properties(torch_cpu PROPERTIES
                INTERFACE_LINK_LIBRARIES
                    "${FELIDAE_TORCH_CPU_LINK_LIBRARIES}")
        endif()
    endif()
    # The prebuilt Windows LibTorch distribution is Release-only.  Linking it
    # into a Debug MSVC binary corrupts STL-owned module registration state at
    # runtime (typically an access violation in std::vector).  Fail during
    # configuration with the corrective command instead of emitting a binary
    # that can crash while constructing a GRU.
    if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR
            "FELIDAE_ENABLE_LIBTORCH requires a Release CMAKE_BUILD_TYPE with the installed LibTorch package. Reconfigure the existing build directory with -DCMAKE_BUILD_TYPE=Release.")
    endif()

    # Keep LibTorch's bundled Protobuf headers out of every translation unit
    # that includes SentencePiece's generated Protobuf classes. The backend
    # communicates with the compiler through integer IDs and typed model
    # interfaces, so it can be compiled as a clean dependency island.
    add_library(felidae_mixfix_torch STATIC src/MixfixStateModel.cpp)
    if(FELIDAE_ENABLE_TRAINING)
        target_sources(felidae_mixfix_torch PRIVATE tools/TrainMixfixGru.cpp)
    endif()
    target_include_directories(felidae_mixfix_torch PRIVATE src)
    target_include_directories(felidae_mixfix_torch SYSTEM PRIVATE
        third_party
        third_party/nlohmann_json/include
        ${TORCH_INCLUDE_DIRS})
    target_compile_definitions(felidae_mixfix_torch PRIVATE
        FELIDAE_HAS_TORCH=1
        FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
    target_link_libraries(felidae_mixfix_torch PRIVATE ${TORCH_LIBRARIES})
endif()

add_executable(felidae_sentencepiece_model_generator EXCLUDE_FROM_ALL
    tools/GenerateSentencePieceModel.cpp)
target_include_directories(felidae_sentencepiece_model_generator PRIVATE src)
target_compile_definitions(felidae_sentencepiece_model_generator PRIVATE
    FELIDAE_GENERATING_MODEL=1)
target_include_directories(felidae_sentencepiece_model_generator BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_include_directories(felidae_sentencepiece_model_generator SYSTEM PRIVATE
    third_party/nlohmann_json/include)
target_link_libraries(felidae_sentencepiece_model_generator PRIVATE sentencepiece_train)
target_link_libraries(felidae_sentencepiece_model_generator PRIVATE nlohmann_json::nlohmann_json)
if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(felidae_sentencepiece_model_generator PRIVATE OpenMP::OpenMP_CXX)
endif()

# The lexical model and matching ID header are versioned release inputs, but
# models/*.model and models/FelidaeSentencePieceIds.h are also .gitignore'd
# (see .gitignore) rather than committed, so a fresh clone or a cleared
# models/ directory legitimately has neither -- there is nothing to "restore
# from Git". Generate them as a normal, ordered step of the build graph
# instead of failing configure: felidae_sentencepiece_model_generator is
# built first, then run once against the authoritative grammar/dataset
# inputs below, and only then do targets that need the model (anything using
# felidae_use_sentencepiece_model()) build. A single normal
# `cmake --build ... --target felidae_compiler felidae_vm` therefore works
# unattended on a machine that has never had models/ populated.
#
# A configure-time `if(NOT EXISTS ...) message(FATAL_ERROR ...)` guard used
# to sit here instead. It made the project unrecoverable through CMake alone
# whenever these files were missing: configure aborted before
# felidae_regenerate_sentencepiece_model (the target that would have fixed
# it) was even defined, and every subsequent configure attempt hit the same
# FATAL_ERROR before reaching it. Do not reintroduce a configure-time
# existence check here for that reason -- the OUTPUT-based custom command
# below is the correct place to encode "build these files if missing".
add_custom_command(
    OUTPUT "${FELIDAE_SENTENCEPIECE_MODEL_PATH}" "${FELIDAE_SENTENCEPIECE_IDS_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FELIDAE_SENTENCEPIECE_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:felidae_sentencepiece_model_generator>
            "${FELIDAE_SENTENCEPIECE_MODEL_PATH}" "${FELIDAE_SENTENCEPIECE_IDS_HEADER}"
            "${FELIDAE_SENTENCEPIECE_DATASET}"
    DEPENDS felidae_sentencepiece_model_generator
            tools/GenerateSentencePieceModel.cpp src/FelidaeGrammar.h
            "${FELIDAE_SENTENCEPIECE_DATASET}"
    COMMENT "Generating the Felidae SentencePiece model and token IDs (runs only when missing or stale)"
    VERBATIM)
add_custom_target(felidae_sentencepiece_model
    DEPENDS "${FELIDAE_SENTENCEPIECE_MODEL_PATH}" "${FELIDAE_SENTENCEPIECE_IDS_HEADER}")

# Explicit alias for a maintainer who wants to force regeneration by name
# (e.g. after intentionally editing the grammar or dataset) without having to
# know the underlying OUTPUT paths. Behaves identically to building
# felidae_sentencepiece_model; kept as a separate name for that discoverability.
add_custom_target(felidae_regenerate_sentencepiece_model
    DEPENDS "${FELIDAE_SENTENCEPIECE_MODEL_PATH}" "${FELIDAE_SENTENCEPIECE_IDS_HEADER}")

function(felidae_use_sentencepiece_model target)
    add_dependencies(${target} felidae_sentencepiece_model)
    # SentencePiece's checked-in generated protobuf classes must see the
    # matching pinned protobuf API before LibTorch's bundled protobuf headers.
    # A normal -I path precedes every -isystem path on GCC/Clang, including
    # /opt/libtorch, while remaining scoped to SentencePiece consumers.
    target_include_directories(${target} BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/protobuf/src"
        "${FELIDAE_SENTENCEPIECE_GENERATED_DIR}")
    target_compile_definitions(${target} PRIVATE
        FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
endfunction()

# Explicit opt-in only: it never runs during normal builds or tests.
add_executable(felidae_integer_parser_benchmark EXCLUDE_FROM_ALL
    tools/IntegerParserBenchmark.cpp
    src/MixfixStateModel.cpp
    src/SentencePieceModel.cpp
    src/IntegerTokenList.cpp
    src/IntegerParser.cpp
    src/BuiltinRegistry.cpp)
target_include_directories(felidae_integer_parser_benchmark PRIVATE src)
target_include_directories(felidae_integer_parser_benchmark BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_compile_definitions(felidae_integer_parser_benchmark PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
target_link_libraries(felidae_integer_parser_benchmark PRIVATE felidae_form sentencepiece)
felidae_use_sentencepiece_model(felidae_integer_parser_benchmark)

# Explicit, bounded source -> verified executable IR -> VM capability benchmark.
# It is excluded from ALL so routine builds never build benchmark workloads.
add_executable(felidae_reasoning_benchmark EXCLUDE_FROM_ALL
    tools/ReasoningBenchmark.cpp
    src/CompilerFrontend.cpp
    src/MixfixStateModel.cpp
    src/IrCodeGenerator.cpp
    src/SentencePieceModel.cpp
    src/IntegerTokenList.cpp
    src/IntegerParser.cpp
    src/BuiltinRegistry.cpp)
target_include_directories(felidae_reasoning_benchmark PRIVATE src)
target_include_directories(felidae_reasoning_benchmark BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_include_directories(felidae_reasoning_benchmark SYSTEM PRIVATE third_party)
target_link_libraries(felidae_reasoning_benchmark PRIVATE felidae_form sentencepiece)
felidae_use_sentencepiece_model(felidae_reasoning_benchmark)
target_compile_options(felidae_reasoning_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)
if(MSVC)
    target_compile_options(felidae_reasoning_benchmark PRIVATE /EHsc)
endif()
set_target_properties(felidae_reasoning_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")

# Corpus extraction reuses the source parser and deterministic IR lowering as
# its only teacher. It is explicit: normal builds never create or retrain ML
# artifacts.
add_executable(felidae_extract_mixfix_dataset EXCLUDE_FROM_ALL
    tools/ExtractMixfixDataset.cpp
    src/CompilerFrontend.cpp
    src/IrCodeGenerator.cpp
    src/MixfixStateModel.cpp
    src/SentencePieceModel.cpp
    src/IntegerTokenList.cpp
    src/IntegerParser.cpp
    src/BuiltinRegistry.cpp)
target_include_directories(felidae_extract_mixfix_dataset PRIVATE src)
target_include_directories(felidae_extract_mixfix_dataset BEFORE PRIVATE
    third_party/sentencepiece third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_include_directories(felidae_extract_mixfix_dataset SYSTEM PRIVATE third_party)
target_compile_definitions(felidae_extract_mixfix_dataset PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
target_link_libraries(felidae_extract_mixfix_dataset PRIVATE felidae_form sentencepiece nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(felidae_extract_mixfix_dataset PRIVATE /EHsc)
endif()
felidae_use_sentencepiece_model(felidae_extract_mixfix_dataset)

function(felidae_enable_sanitizers target)
    if(NOT FELIDAE_ENABLE_SANITIZERS)
        return()
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR
            "FELIDAE_ENABLE_SANITIZERS is unsupported by ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

function(felidae_enable_quality_warnings target)
    if(NOT FELIDAE_ENABLE_STRICT_WARNINGS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /EHsc /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wconversion -Wsign-conversion -Wformat=2 -Wformat-overflow=2
            -Wformat-truncation=2 -Wstringop-overflow=4 -Wstringop-truncation
            -Warray-bounds=2 -Walloc-zero -Walloca -Wcast-align -Wcast-qual
            -Wdouble-promotion -Wduplicated-cond -Wduplicated-branches -Wlogical-op
            -Wnull-dereference -Wpointer-arith -Wredundant-decls -Wswitch-enum
            -Wundef -Wuninitialized -Wmaybe-uninitialized -Wuse-after-free=3 -Wvla
            -Wunused -Wunused-function -Wunused-variable -Wunused-parameter)
    endif()
endfunction()

function(felidae_enable_static_analysis target)
    if(MSVC AND FELIDAE_ENABLE_MSVC_STATIC_ANALYSIS)
        target_compile_options(${target} PRIVATE /analyze /analyze:external-)
    endif()
endfunction()

function(felidae_enable_msvc_exceptions target)
    if(MSVC)
        # Source, IR, and binary diagnostics use exceptions.  Scope this to
        # Felidae targets so dependency builds retain their upstream flags.
        target_compile_options(${target} PRIVATE /EHsc)
    endif()
endfunction()

if(TARGET felidae_mixfix_torch)
    felidae_enable_msvc_exceptions(felidae_mixfix_torch)
    felidae_enable_static_analysis(felidae_mixfix_torch)
endif()

# Form is the AST/parser-free executable IR, binary, and register-VM boundary. Keeping it
# as one library prevents the compiler and standalone VM from compiling two
# subtly different copies of the runtime.
add_library(felidae_form STATIC
    src/form/BinaryIr.cpp
    src/form/IrModule.cpp
    src/form/RegisterVm.cpp
    src/form/RuntimeTraining.cpp
    src/form/libs/Builtin.cpp
    src/form/libs/Csv.cpp
    src/form/libs/Db.cpp
    src/form/libs/Group.cpp
    src/form/libs/Json.cpp
    src/form/libs/Set.cpp)
target_include_directories(felidae_form PUBLIC src)
target_link_libraries(felidae_form PRIVATE nlohmann_json::nlohmann_json)
felidae_enable_static_analysis(felidae_form)
felidae_enable_msvc_exceptions(felidae_form)

if(TARGET felidae_mixfix_torch)
    target_link_libraries(felidae_mixfix_torch PRIVATE felidae_form)
endif()

add_executable(felidae_vm
    src/VmMain.cpp
    src/SentencePieceModel.cpp)
target_link_libraries(felidae_vm PRIVATE felidae_form sentencepiece)
target_include_directories(felidae_vm BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_compile_definitions(felidae_vm PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
felidae_use_sentencepiece_model(felidae_vm)
felidae_enable_msvc_exceptions(felidae_vm)
set_target_properties(felidae_vm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")
felidae_enable_static_analysis(felidae_vm)

if(FELIDAE_ENABLE_LIBTORCH)
    # The daemon owns an optional runtime-only GRU. Form itself remains free
    # of LibTorch and all compiler/parser dependencies.
    target_sources(felidae_vm PRIVATE
        src/form/LibTorchTensorRuntime.cpp
        src/form/RuntimeStateModel.cpp)
    if(FELIDAE_ENABLE_TRAINING)
        target_sources(felidae_vm PRIVATE tools/TrainRuntimeGru.cpp)
        target_compile_definitions(felidae_vm PRIVATE FELIDAE_HAS_TRAINING=1)
    endif()
    target_compile_definitions(felidae_vm PRIVATE FELIDAE_HAS_TORCH=1)
    target_include_directories(felidae_vm SYSTEM PRIVATE ${TORCH_INCLUDE_DIRS})
    target_link_libraries(felidae_vm PRIVATE ${TORCH_LIBRARIES})
endif()

# Portable shipping folder. Release staging is a build product and therefore
# remains inside the selected build tree instead of modifying the source tree.
set(FELIDAE_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
set(FELIDAE_DIST_COMMAND
    "${CMAKE_COMMAND}"
    "-DFELIDAE_DIST_DIR=${FELIDAE_DIST_DIR}"
    "-DFELIDAE_DIST_BUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DFELIDAE_DIST_CONFIG=$<CONFIG>"
    "-DFELIDAE_DIST_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
    "-DFELIDAE_DIST_MODEL_DIR=${CMAKE_SOURCE_DIR}/models"
    "-DFELIDAE_DIST_LICENSE_DIR=${CMAKE_SOURCE_DIR}/third_party"
    "-DFELIDAE_DIST_VERSION=${FELIDAE_VERSION}")
if(FELIDAE_ENABLE_LIBTORCH)
    list(APPEND FELIDAE_DIST_COMMAND "-DFELIDAE_DIST_HAS_LIBTORCH=1")
    list(APPEND FELIDAE_DIST_COMMAND
        "-DFELIDAE_DIST_TORCH_ROOT=${TORCH_INSTALL_PREFIX}")
else()
    list(APPEND FELIDAE_DIST_COMMAND "-DFELIDAE_DIST_HAS_LIBTORCH=0")
endif()
list(APPEND FELIDAE_DIST_COMMAND "-P" "${CMAKE_SOURCE_DIR}/cmake/StageDistribution.cmake")
add_custom_target(felidae_dist
    COMMAND ${FELIDAE_DIST_COMMAND}
    DEPENDS felidae_compiler felidae_vm felidae_debugger felidae_sentencepiece_model
    COMMENT "Staging portable Felidae distribution in ${FELIDAE_DIST_DIR}")

# Explicit runtime-corpus boundary. The tool accepts only verified .bin input
# and emits a unary Identity teacher for its typed result. Symbol context is
# stored as complete SentencePiece ID sequences. It never
# hashes source names into semantic operation IDs or invents context queries.
add_executable(felidae_build_runtime_dataset EXCLUDE_FROM_ALL
    tools/BuildRuntimeTrainingDataset.cpp
    src/SentencePieceModel.cpp)
target_link_libraries(felidae_build_runtime_dataset PRIVATE felidae_form sentencepiece)
target_include_directories(felidae_build_runtime_dataset BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_compile_definitions(felidae_build_runtime_dataset PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
felidae_use_sentencepiece_model(felidae_build_runtime_dataset)
felidae_enable_msvc_exceptions(felidae_build_runtime_dataset)
set_target_properties(felidae_build_runtime_dataset PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8)
    message(FATAL_ERROR "Felidae requires GCC 8 or newer for std::filesystem. Use GCC 8+, Clang 7+, AppleClang 10+, or MSVC 2019+.")
endif()

add_executable(felidae_compiler
    src/main.cpp
    src/CompilerFrontend.cpp
    src/IrCodeGenerator.cpp
    src/SentencePieceModel.cpp
    src/IntegerTokenList.cpp
    src/IntegerParser.cpp
    src/BuiltinRegistry.cpp
)
# The command-line entry point has a stable location on every generator.
# In particular, Visual Studio's multi-config generator otherwise places it
# below build\\Debug or build\\Release, which makes build.cmd/build.ps1
# unexpectedly fail to leave build\\felidae_compiler.exe behind.
set_target_properties(felidae_compiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")

target_include_directories(felidae_compiler PRIVATE src)
target_include_directories(felidae_compiler BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_include_directories(felidae_compiler SYSTEM PRIVATE third_party)
target_compile_definitions(felidae_compiler PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
felidae_enable_msvc_exceptions(felidae_compiler)
target_link_libraries(felidae_compiler PRIVATE felidae_form sentencepiece nlohmann_json::nlohmann_json)
if(FELIDAE_ENABLE_LIBTORCH)
    target_compile_definitions(felidae_compiler PRIVATE FELIDAE_HAS_TORCH=1)
    if(FELIDAE_ENABLE_TRAINING)
        target_compile_definitions(felidae_compiler PRIVATE FELIDAE_HAS_TRAINING=1)
    endif()
    target_link_libraries(felidae_compiler PRIVATE felidae_mixfix_torch)
    # Training is an explicit subcommand of felidae_compiler/felidae_vm.  Do
    # not create separate trainer executables in build/.
else()
    target_sources(felidae_compiler PRIVATE src/MixfixStateModel.cpp)
endif()
if(FELIDAE_ENABLE_LIBTORCH)
    foreach(FELIDAE_TORCH_EXECUTABLE felidae_compiler felidae_vm)
        add_custom_command(TARGET ${FELIDAE_TORCH_EXECUTABLE} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DFELIDAE_EXECUTABLE=$<TARGET_FILE:${FELIDAE_TORCH_EXECUTABLE}>"
                "-DFELIDAE_TORCH_LIB_DIR=${TORCH_INSTALL_PREFIX}/lib"
                "-DFELIDAE_OUTPUT_DIR=$<TARGET_FILE_DIR:${FELIDAE_TORCH_EXECUTABLE}>"
                "-DFELIDAE_RUNTIME_MANIFEST=${CMAKE_BINARY_DIR}/felidae-torch-runtime-${FELIDAE_TORCH_EXECUTABLE}.txt"
                -P "${CMAKE_SOURCE_DIR}/cmake/StageTorchRuntime.cmake"
            COMMENT "Staging resolved LibTorch DLLs beside ${FELIDAE_TORCH_EXECUTABLE}")
    endforeach()
endif()
felidae_use_sentencepiece_model(felidae_compiler)
target_compile_options(felidae_compiler PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(felidae_compiler PRIVATE dl)
endif()
felidae_enable_sanitizers(felidae_compiler)
felidae_enable_static_analysis(felidae_compiler)

set(FELIDAE_DEBUG_SOURCES
    src/debugger/main.cpp
    src/debugger/AstAnalyzer.cpp
    src/tooling/SourceParser.cpp
    src/CompilerFrontend.cpp
    src/IrCodeGenerator.cpp
    src/MixfixStateModel.cpp
    src/SentencePieceModel.cpp
    src/IntegerTokenList.cpp
    src/IntegerParser.cpp
    src/BuiltinRegistry.cpp
)

# The source debugger is an explicit developer tool.  Leaving it out of ALL
# prevents a normal Debug/RelWithDebInfo build from compiling a second copy of
# the compiler frontend.
add_executable(felidae_debugger EXCLUDE_FROM_ALL ${FELIDAE_DEBUG_SOURCES})
set_target_properties(felidae_debugger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")
target_include_directories(felidae_debugger PRIVATE src)
target_include_directories(felidae_debugger BEFORE PRIVATE
    third_party/sentencepiece
    third_party/sentencepiece/src
    ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
target_include_directories(felidae_debugger SYSTEM PRIVATE third_party)
target_compile_definitions(felidae_debugger PRIVATE
    FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}")
target_link_libraries(felidae_debugger PRIVATE felidae_form sentencepiece
    nlohmann_json::nlohmann_json)
felidae_use_sentencepiece_model(felidae_debugger)
if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(felidae_debugger PRIVATE OpenMP::OpenMP_CXX)
endif()
target_compile_options(felidae_debugger PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
felidae_enable_sanitizers(felidae_debugger)

if(FELIDAE_BUILD_TESTS)
    enable_testing()
    add_executable(felidae_sentencepiece_model_test
        tests/SentencePieceModelTest.cpp
        src/MixfixStateModel.cpp
        src/SentencePieceModel.cpp
        src/IntegerTokenList.cpp
        src/IntegerParser.cpp
        src/BuiltinRegistry.cpp)
    target_include_directories(felidae_sentencepiece_model_test PRIVATE src)
    target_include_directories(felidae_sentencepiece_model_test BEFORE PRIVATE
        third_party/sentencepiece
        third_party/sentencepiece/src
        ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
    target_include_directories(felidae_sentencepiece_model_test SYSTEM PRIVATE third_party)
    target_compile_definitions(felidae_sentencepiece_model_test PRIVATE
        FELIDAE_SENTENCEPIECE_MODEL_PATH="${FELIDAE_SENTENCEPIECE_MODEL_PATH}"
        FELIDAE_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-artifacts/sentencepiece-model")
    # The regression executable intentionally uses assertions.  Keep them
    # active even when the rest of Felidae is built in Release mode.
    target_compile_options(felidae_sentencepiece_model_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    felidae_enable_msvc_exceptions(felidae_sentencepiece_model_test)
    # This frontend regression compiles executable IR and exercises the same
    # verifier implementation linked by the runtime-only felidae_form library.
    target_link_libraries(felidae_sentencepiece_model_test PRIVATE felidae_form sentencepiece)
    felidae_use_sentencepiece_model(felidae_sentencepiece_model_test)
    if(TARGET OpenMP::OpenMP_CXX)
        target_link_libraries(felidae_sentencepiece_model_test PRIVATE OpenMP::OpenMP_CXX)
    endif()
    add_test(NAME felidae_sentencepiece_model COMMAND felidae_sentencepiece_model_test)

    # The tokenizer is a production compiler command, not a helper executable.
    # Assert its machine-readable, integer-only output at the CLI boundary.
    add_test(NAME felidae_compiler_tokenize_cli
        COMMAND $<TARGET_FILE:felidae_compiler> --tokenize
                ${CMAKE_SOURCE_DIR}/v2_examples/mixfix_nested_expression.fx)
    set_tests_properties(felidae_compiler_tokenize_cli PROPERTIES
        TIMEOUT 30
        PASS_REGULAR_EXPRESSION "^\\[[0-9,]+\\]"
        ENVIRONMENT "PATH=${TORCH_INSTALL_PREFIX}/lib;$ENV{PATH}")

    add_executable(felidae_sentencepiece_pipeline_test
        tests/SentencePiecePipelineTest.cpp
        src/CompilerFrontend.cpp
        src/MixfixStateModel.cpp
        src/IrCodeGenerator.cpp
        src/SentencePieceModel.cpp
        src/IntegerTokenList.cpp
        src/IntegerParser.cpp
        src/BuiltinRegistry.cpp)
    target_include_directories(felidae_sentencepiece_pipeline_test PRIVATE src)
    target_include_directories(felidae_sentencepiece_pipeline_test BEFORE PRIVATE
        third_party/sentencepiece
        third_party/sentencepiece/src
        ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
    target_include_directories(felidae_sentencepiece_pipeline_test SYSTEM PRIVATE third_party)
    target_compile_definitions(felidae_sentencepiece_pipeline_test PRIVATE
        FELIDAE_PIPELINE_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/v2_examples/sentencepiece_pipeline.fx"
        FELIDAE_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-artifacts/sentencepiece-pipeline"
        FELIDAE_FACT_EXPRESSION_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/v2_examples/fact_expression_result_contract.fx"
        FELIDAE_COFFEE_REASONING_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/v2_examples/coffee_vending_theorem_solver.fx"
        FELIDAE_HVAC_REASONING_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/v2_examples/air_conditioner_theorem_solver.fx")
    target_compile_definitions(felidae_sentencepiece_pipeline_test PRIVATE
        FELIDAE_NUMERIC_OPERATIONS_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/v2_examples/numeric_operations.fx")
    target_compile_options(felidae_sentencepiece_pipeline_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    felidae_enable_msvc_exceptions(felidae_sentencepiece_pipeline_test)
    target_link_libraries(felidae_sentencepiece_pipeline_test PRIVATE felidae_form sentencepiece)
    felidae_use_sentencepiece_model(felidae_sentencepiece_pipeline_test)
    if(TARGET OpenMP::OpenMP_CXX)
        target_link_libraries(felidae_sentencepiece_pipeline_test PRIVATE OpenMP::OpenMP_CXX)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(felidae_sentencepiece_pipeline_test PRIVATE dl)
    endif()
    add_test(NAME felidae_sentencepiece_pipeline COMMAND felidae_sentencepiece_pipeline_test)
    set_tests_properties(felidae_sentencepiece_pipeline PROPERTIES TIMEOUT 30)
    # Avoid stale Windows handle contention from an interrupted prior test run.
    set_target_properties(felidae_sentencepiece_pipeline_test PROPERTIES
        OUTPUT_NAME felidae_sentencepiece_pipeline_ir_test)

    add_executable(felidae_mixfix_state_model_test
        tests/MixfixStateModelTest.cpp
        src/form/RuntimeStateModel.cpp)
    target_include_directories(felidae_mixfix_state_model_test PRIVATE src)
    if(FELIDAE_ENABLE_LIBTORCH)
        target_sources(felidae_mixfix_state_model_test PRIVATE
            src/CompilerFrontend.cpp
            src/IrCodeGenerator.cpp
            src/SentencePieceModel.cpp
            src/IntegerTokenList.cpp
            src/IntegerParser.cpp
            src/BuiltinRegistry.cpp)
        target_include_directories(felidae_mixfix_state_model_test BEFORE PRIVATE
            third_party/sentencepiece
            third_party/sentencepiece/src
            ${CMAKE_BINARY_DIR}/third_party/sentencepiece/src)
        target_include_directories(felidae_mixfix_state_model_test SYSTEM PRIVATE third_party)
        target_link_libraries(felidae_mixfix_state_model_test PRIVATE sentencepiece)
        felidae_use_sentencepiece_model(felidae_mixfix_state_model_test)
        # SentencePiece and LibTorch remain separate translation-unit
        # dependency islands even in this combined E2E test. The real compiler
        # also emits this FELBIR fixture under build/, after which the test
        # loads it through the production binary verifier and executes its
        # SemanticEval instruction with a trained, exported, and reloaded
        # TorchScript backend.
        set(FELIDAE_RUNTIME_SSM_E2E_SOURCE
            "${CMAKE_SOURCE_DIR}/v2_examples/runtime_ssm_e2e.fx")
        set(FELIDAE_RUNTIME_SSM_E2E_BINARY
            "${CMAKE_BINARY_DIR}/runtime_ssm_e2e.bin")
        add_custom_command(
            OUTPUT "${FELIDAE_RUNTIME_SSM_E2E_BINARY}"
            COMMAND $<TARGET_FILE:felidae_compiler>
                    "${FELIDAE_RUNTIME_SSM_E2E_SOURCE}"
            DEPENDS felidae_compiler "${FELIDAE_RUNTIME_SSM_E2E_SOURCE}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Compiling runtime SSM end-to-end FELBIR fixture"
            VERBATIM)
        add_custom_target(felidae_runtime_ssm_e2e_binary
            DEPENDS "${FELIDAE_RUNTIME_SSM_E2E_BINARY}")
        add_dependencies(felidae_mixfix_state_model_test
            felidae_runtime_ssm_e2e_binary)
        target_compile_definitions(felidae_mixfix_state_model_test PRIVATE
            FELIDAE_RUNTIME_SSM_E2E_BINARY="${FELIDAE_RUNTIME_SSM_E2E_BINARY}"
            FELIDAE_MODEL_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/model-tests")
    endif()
    target_compile_options(felidae_mixfix_state_model_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    felidae_enable_msvc_exceptions(felidae_mixfix_state_model_test)
    if(FELIDAE_ENABLE_LIBTORCH)
        target_compile_definitions(felidae_mixfix_state_model_test PRIVATE FELIDAE_HAS_TORCH=1)
        target_include_directories(felidae_mixfix_state_model_test SYSTEM PRIVATE ${TORCH_INCLUDE_DIRS})
        target_link_libraries(felidae_mixfix_state_model_test PRIVATE felidae_mixfix_torch ${TORCH_LIBRARIES})
        add_executable(felidae_libtorch_smoke_test
            tests/LibTorchSmoke.cpp
            src/form/LibTorchTensorRuntime.cpp)
        target_include_directories(felidae_libtorch_smoke_test PRIVATE src)
        target_include_directories(felidae_libtorch_smoke_test SYSTEM PRIVATE ${TORCH_INCLUDE_DIRS})
        target_link_libraries(felidae_libtorch_smoke_test PRIVATE felidae_form ${TORCH_LIBRARIES})
        target_compile_options(felidae_libtorch_smoke_test PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
            $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
        add_test(NAME felidae_libtorch_smoke COMMAND felidae_libtorch_smoke_test)
        set_tests_properties(felidae_libtorch_smoke PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "PATH=${TORCH_INSTALL_PREFIX}/lib;$ENV{PATH}")
    else()
        target_sources(felidae_mixfix_state_model_test PRIVATE src/MixfixStateModel.cpp)
    endif()
    add_test(NAME felidae_mixfix_state_model COMMAND felidae_mixfix_state_model_test)
    if(FELIDAE_ENABLE_LIBTORCH)
        set_tests_properties(felidae_mixfix_state_model PROPERTIES
            TIMEOUT 30
            ENVIRONMENT "PATH=${TORCH_INSTALL_PREFIX}/lib;$ENV{PATH}")
    else()
        set_tests_properties(felidae_mixfix_state_model PROPERTIES TIMEOUT 30)
    endif()
endif()

if(FELIDAE_BUILD_TESTS)
    # This round-trip test uses the compiler-only lowerer to construct its
    # fixture. The isolation test below proves the VM-only link boundary.
    add_executable(felidae_form_binary_test tests/FormBinaryIrTest.cpp)
    target_link_libraries(felidae_form_binary_test PRIVATE felidae_form)
    target_compile_definitions(felidae_form_binary_test PRIVATE
        FELIDAE_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-artifacts/form-binary")
    target_compile_options(felidae_form_binary_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    felidae_enable_msvc_exceptions(felidae_form_binary_test)
    target_link_libraries(felidae_mixfix_state_model_test PRIVATE felidae_form)
    add_test(NAME felidae_form_binary COMMAND felidae_form_binary_test)
    set_tests_properties(felidae_form_binary PROPERTIES TIMEOUT 30)

    # This target includes only Form headers and links only felidae_form. It
    # prevents AST/parser/SentencePiece headers from leaking across the VM
    # boundary as the repository is refactored.
    add_executable(felidae_form_isolation_test tests/FormIsolationTest.cpp)
    target_link_libraries(felidae_form_isolation_test PRIVATE felidae_form)
    target_compile_options(felidae_form_isolation_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    felidae_enable_msvc_exceptions(felidae_form_isolation_test)
    add_test(NAME felidae_form_isolation COMMAND felidae_form_isolation_test)
    set_tests_properties(felidae_form_isolation PROPERTIES TIMEOUT 30)

    add_executable(felidae_ir_test tests/FelidaeIrTest.cpp)
    target_link_libraries(felidae_ir_test PRIVATE felidae_form Threads::Threads
        nlohmann_json::nlohmann_json)
    target_compile_definitions(felidae_ir_test PRIVATE
        FELIDAE_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-artifacts/ir")
    target_compile_options(felidae_ir_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    felidae_enable_msvc_exceptions(felidae_ir_test)
    add_test(NAME felidae_ir COMMAND felidae_ir_test)
    set_tests_properties(felidae_ir PROPERTIES TIMEOUT 30)

    # CSV <-> Fact, round trip, ownership, and atomic automatic persistence
    # against real files in a temporary output directory (no mocked
    # persistence).
    add_executable(felidae_db_csv_test tests/FormDbCsvTest.cpp)
    target_link_libraries(felidae_db_csv_test PRIVATE felidae_form
        nlohmann_json::nlohmann_json)
    target_compile_definitions(felidae_db_csv_test PRIVATE
        FELIDAE_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-artifacts/db-csv")
    target_compile_options(felidae_db_csv_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    felidae_enable_msvc_exceptions(felidae_db_csv_test)
    add_test(NAME felidae_db_csv COMMAND felidae_db_csv_test)
    set_tests_properties(felidae_db_csv PROPERTIES TIMEOUT 30)

    # Exercise representative user programs through the public compiler and
    # VM executables. Keep generated .bin files in the active build tree.
    # External-service, training, and stress fixtures have separate
    # prerequisites and therefore do not belong in this ordinary smoke set.
    set(FELIDAE_RUNNABLE_V2_EXAMPLES
        form_core_concepts
        numeric_operations
        json_csv_operations
        csv_fact_database
        group_operations
        set_operations
        degree_profiles
        hierarchical_fact_filter
        fact_expression_result_contract
        where_guard_chain
        optional_end_blocks
        end_then_pipeline
        coffee_vending_theorem_solver
        air_conditioner_theorem_solver
        mixfix_ir_roundtrip
        operator_expression_inventory
        standard_search_algorithms
        deep_fact_reasoning_analysis
        selective_relationship_reasoning)
    foreach(example IN LISTS FELIDAE_RUNNABLE_V2_EXAMPLES)
        add_test(NAME felidae_example_${example}
            COMMAND "${CMAKE_COMMAND}"
                "-DFELIDAE_COMPILER=$<TARGET_FILE:felidae_compiler>"
                "-DFELIDAE_VM=$<TARGET_FILE:felidae_vm>"
                "-DFELIDAE_EXAMPLE=${CMAKE_SOURCE_DIR}/v2_examples/${example}.fx"
                "-DFELIDAE_BINARY_DIR=$<TARGET_FILE_DIR:felidae_compiler>"
                "-DFELIDAE_RUNTIME_WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}"
                -P "${CMAKE_SOURCE_DIR}/cmake/RunFelidaeExample.cmake")
        set_tests_properties(felidae_example_${example} PROPERTIES
            TIMEOUT 30
            LABELS "examples;integration")
    endforeach()
    set_tests_properties(felidae_example_json_csv_operations PROPERTIES
        PASS_REGULAR_EXPRESSION "name: Ada")
    # renderNumber() only forces a trailing .0 on exactly 0.0/1.0 (the
    # boolean convention); an ordinary whole number like this count prints
    # without one, so the assertion must match that, not assume every number
    # gets ".0".
    set_tests_properties(felidae_example_csv_fact_database PROPERTIES
        PASS_REGULAR_EXPRESSION "all_count: 4, active_central_count: 2")
    set_tests_properties(felidae_example_group_operations PROPERTIES
        PASS_REGULAR_EXPRESSION "abelian: 1.0")
    set_tests_properties(felidae_example_set_operations PROPERTIES
        PASS_REGULAR_EXPRESSION "contains: 1.0")
    set_tests_properties(felidae_example_operator_expression_inventory PROPERTIES
        PASS_REGULAR_EXPRESSION "precedence: 14, grouped: 20, unary: -7, ordered: 1.0, unequal: 1.0, eligible: 1.0, member: Ava, pipeline: 22")
    set_tests_properties(felidae_example_standard_search_algorithms PROPERTIES
        PASS_REGULAR_EXPRESSION "linear_last: 63, binary_last: 63, missing: \\{linear: -1, binary: -1\\}")
    set_tests_properties(felidae_example_deep_fact_reasoning_analysis PROPERTIES
        PASS_REGULAR_EXPRESSION "directed_cat_tiger: \\[\\], symmetric_tiger_cat: \\[\\{from:")
    set_tests_properties(felidae_example_selective_relationship_reasoning PROPERTIES
        PASS_REGULAR_EXPRESSION "connected_via_signal: 1.0\\}")
    set_tests_properties(felidae_example_where_guard_chain PROPERTIES
        PASS_REGULAR_EXPRESSION "passes: 82, lowScore: 0.0, inactive: 0.0")
    set_tests_properties(felidae_example_optional_end_blocks PROPERTIES
        PASS_REGULAR_EXPRESSION "high: 2, middle: 1.0, low: 0.0")
    set_tests_properties(felidae_example_end_then_pipeline PROPERTIES
        PASS_REGULAR_EXPRESSION "accepted: 10, rejected: 0.0")

    # Tests are deliberately opt-in at build time.  A normal IDE build should
    # compile only the two production executables and their required
    # dependencies; `--target felidae_tests` builds the complete test suite.
    set(FELIDAE_TEST_TARGETS
        felidae_sentencepiece_model_test
        felidae_sentencepiece_pipeline_test
        felidae_mixfix_state_model_test
        felidae_form_binary_test
        felidae_form_isolation_test
        felidae_ir_test
        felidae_db_csv_test)
    if(TARGET felidae_libtorch_smoke_test)
        list(APPEND FELIDAE_TEST_TARGETS felidae_libtorch_smoke_test)
    endif()
    foreach(test_target IN LISTS FELIDAE_TEST_TARGETS)
        set_property(TARGET ${test_target} PROPERTY EXCLUDE_FROM_ALL TRUE)
    endforeach()
    add_custom_target(felidae_tests
        DEPENDS ${FELIDAE_TEST_TARGETS} felidae_compiler felidae_vm felidae_debugger)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/Eigen/Eigen/Core")
    # Eigen is header-only.  Expose the pinned submodule through the canonical
    # target without configuring Eigen's upstream test/probe project.
    add_library(felidae_eigen INTERFACE)
    target_include_directories(felidae_eigen INTERFACE
        "${CMAKE_SOURCE_DIR}/third_party/Eigen")
    add_library(Eigen3::Eigen ALIAS felidae_eigen)
    target_compile_definitions(felidae_compiler PRIVATE FELIDAE_HAS_EIGEN=1)
    target_link_libraries(felidae_compiler PRIVATE Eigen3::Eigen)
endif()

foreach(felidae_quality_target IN ITEMS
        felidae_sentencepiece_model_generator felidae_integer_parser_benchmark
        felidae_extract_mixfix_dataset
        felidae_form felidae_mixfix_torch felidae_compiler felidae_vm felidae_debugger
        felidae_sentencepiece_model_test felidae_sentencepiece_pipeline_test
        felidae_mixfix_state_model_test)
    if(TARGET ${felidae_quality_target})
        felidae_enable_quality_warnings(${felidae_quality_target})
    endif()
endforeach()

if(FELIDAE_BUILD_TESTS)
    # Release gate: build the portable distribution, run the focused IR/VM
    # regression suite, then execute a compiler->.bin->VM smoke through the
    # staged binaries themselves. It never creates a second build tree.
    add_custom_target(felidae_beta
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
                -R felidae_
                --output-on-failure
        COMMAND "${CMAKE_COMMAND}"
                "-DFELIDAE_BETA_DIST_DIR=${FELIDAE_DIST_DIR}"
                "-DFELIDAE_BETA_BUILD_DIR=${CMAKE_BINARY_DIR}"
                "-DFELIDAE_BETA_EXAMPLE=${CMAKE_SOURCE_DIR}/v2_examples/mixfix_deep_ir_nesting.fx"
                -P "${CMAKE_SOURCE_DIR}/cmake/BetaReleaseCheck.cmake"
        DEPENDS felidae_dist felidae_sentencepiece_model_test felidae_sentencepiece_pipeline_test
                felidae_mixfix_state_model_test felidae_form_binary_test
                felidae_form_isolation_test felidae_ir_test
        COMMENT "Running Felidae beta release gate")
endif()
