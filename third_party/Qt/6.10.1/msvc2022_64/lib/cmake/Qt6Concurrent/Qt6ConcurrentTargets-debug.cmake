#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::Concurrent" for configuration "Debug"
set_property(TARGET Qt6::Concurrent APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::Concurrent PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6Concurrentd.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6Concurrentd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::Concurrent )
list(APPEND _cmake_import_check_files_for_Qt6::Concurrent "${_IMPORT_PREFIX}/lib/Qt6Concurrentd.lib" "${_IMPORT_PREFIX}/bin/Qt6Concurrentd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
