if(NOT DEFINED FELIDAE_EXECUTABLE OR NOT DEFINED PROJECT_DIRECTORY OR
   NOT DEFINED DATABASE_DIRECTORY)
  message(FATAL_ERROR "Felidae database cleanup arguments are incomplete")
endif()

# A prior interrupted test run may have left the local owner alive. Ask it to
# release RocksDB before removing the fixture database; absence is already a
# clean state and is intentionally ignored.
if(EXISTS "${PROJECT_DIRECTORY}/init.fx")
  execute_process(
    COMMAND "${FELIDAE_EXECUTABLE}" db stop "${PROJECT_DIRECTORY}"
    OUTPUT_QUIET ERROR_QUIET)
endif()
file(REMOVE_RECURSE "${DATABASE_DIRECTORY}")
if(EXISTS "${DATABASE_DIRECTORY}")
  message(FATAL_ERROR "Could not remove test database: ${DATABASE_DIRECTORY}")
endif()
