if(NOT DEFINED DATABASE_DIRECTORY)
  message(FATAL_ERROR "Felidae database cleanup arguments are incomplete")
endif()

file(REMOVE_RECURSE "${DATABASE_DIRECTORY}")
if(EXISTS "${DATABASE_DIRECTORY}")
  message(FATAL_ERROR "Could not remove test database: ${DATABASE_DIRECTORY}")
endif()
