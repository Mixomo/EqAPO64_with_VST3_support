#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::Network" for configuration "Debug"
set_property(TARGET Qt6::Network APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::Network PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6Networkd.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6Networkd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::Network )
list(APPEND _cmake_import_check_files_for_Qt6::Network "${_IMPORT_PREFIX}/lib/Qt6Networkd.lib" "${_IMPORT_PREFIX}/bin/Qt6Networkd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
