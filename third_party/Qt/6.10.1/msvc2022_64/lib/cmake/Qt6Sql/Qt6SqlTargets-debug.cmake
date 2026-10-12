#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::Sql" for configuration "Debug"
set_property(TARGET Qt6::Sql APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::Sql PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6Sqld.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6Sqld.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::Sql )
list(APPEND _cmake_import_check_files_for_Qt6::Sql "${_IMPORT_PREFIX}/lib/Qt6Sqld.lib" "${_IMPORT_PREFIX}/bin/Qt6Sqld.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
