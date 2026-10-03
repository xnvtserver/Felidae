if(NOT DEFINED FELIDAE_EXECUTABLE OR NOT DEFINED FELIDAE_TEST_ROOT)
  message(FATAL_ERROR "RunFelidaeProjectTest requires FELIDAE_EXECUTABLE and FELIDAE_TEST_ROOT")
endif()

set(source "")
set(arguments)
math(EXPR last_argument "${CMAKE_ARGC} - 1")
foreach(index RANGE 0 ${last_argument})
  if(source STREQUAL "" AND CMAKE_ARGV${index} MATCHES "[.]fx$")
    set(source "${CMAKE_ARGV${index}}")
  elseif(NOT source STREQUAL "")
    list(APPEND arguments "${CMAKE_ARGV${index}}")
  endif()
endforeach()
if(source STREQUAL "")
  message(FATAL_ERROR "RunFelidaeProjectTest did not receive a Felidae source file")
endif()

get_filename_component(source "${source}" ABSOLUTE)
get_filename_component(source_directory "${source}" DIRECTORY)
get_filename_component(source_name "${source}" NAME)
get_filename_component(project_kind "${source_directory}" NAME)

# Only the ordered fixture members below share durable state. Other programs
# in the same source directory are independent regressions and may run in
# parallel without observing or deleting the fixture database.
set(persistent_sources
  rocks_persistence_seed.fx
  rocks_persistence_reseed.fx
  rocks_persistence_read.fx
  rocks_schema_seed.fx
  rocks_schema_open_shape.fx
  atomic_delete_seed_fail.fx
  atomic_delete_read.fx
  schemaless_promotion_seed.fx
  schemaless_promotion_class.fx
  rocks_index_seed.fx
  rocks_index_update.fx
  rocks_index_read.fx
  rocks_designation_seed.fx
  rocks_designation_read.fx
  rocks_inheritance_seed.fx
  rocks_inheritance_read.fx
  rocks_config_set.fx
  rocks_config_read.fx)
list(FIND persistent_sources "${source_name}" persistent_index)

if(persistent_index EQUAL -1)
  string(JOIN "|" argument_key ${arguments})
  string(SHA256 project_hash "${source}|${argument_key}")
  set(project_directory "${FELIDAE_TEST_ROOT}/${project_hash}")
  file(REMOVE_RECURSE "${project_directory}")
  file(MAKE_DIRECTORY "${project_directory}")
  file(COPY "${source_directory}/" DESTINATION "${project_directory}")
  file(WRITE "${project_directory}/init.fx"
    "import \"db\".\n"
    "db.location(\"./data.db\").\n")
  set(execution_source "${project_directory}/${source_name}")
else()
  set(project_directory "${FELIDAE_TEST_ROOT}/persistent/${project_kind}")
  file(MAKE_DIRECTORY "${project_directory}")
  file(COPY "${source_directory}/" DESTINATION "${project_directory}")
  file(WRITE "${project_directory}/init.fx"
    "import \"db\".\n"
    "db.location(\"./data.db\").\n")
  set(execution_source "${project_directory}/${source_name}")
endif()

# fx.interpret resolves a relative file: against the working directory, and the
# database records the source path of the copy run here. Tests that pass such a
# path opt in with FELIDAE_TEST_CWD_IS_PROJECT; every other test keeps the
# working directory ctest gave it.
if(DEFINED ENV{FELIDAE_TEST_CWD_IS_PROJECT})
  execute_process(
    COMMAND "${FELIDAE_EXECUTABLE}" "${execution_source}" ${arguments}
    WORKING_DIRECTORY "${project_directory}"
    RESULT_VARIABLE result)
else()
  execute_process(
    COMMAND "${FELIDAE_EXECUTABLE}" "${execution_source}" ${arguments}
    RESULT_VARIABLE result)
endif()

if(NOT result EQUAL 0)
  message(FATAL_ERROR "Felidae regression exited with code ${result}")
endif()
