#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QPSQLDriverPlugin" for configuration "Debug"
set_property(TARGET Qt6::QPSQLDriverPlugin APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QPSQLDriverPlugin PROPERTIES
  IMPORTED_COMMON_LANGUAGE_RUNTIME_DEBUG ""
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/plugins/sqldrivers/qsqlpsqld.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QPSQLDriverPlugin )
list(APPEND _cmake_import_check_files_for_Qt6::QPSQLDriverPlugin "${_IMPORT_PREFIX}/plugins/sqldrivers/qsqlpsqld.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
