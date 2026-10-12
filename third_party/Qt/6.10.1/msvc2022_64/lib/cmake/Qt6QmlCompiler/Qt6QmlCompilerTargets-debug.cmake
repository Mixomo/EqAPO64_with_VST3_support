#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QmlCompiler" for configuration "Debug"
set_property(TARGET Qt6::QmlCompiler APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QmlCompiler PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QmlCompilerd.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QmlCompilerd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QmlCompiler )
list(APPEND _cmake_import_check_files_for_Qt6::QmlCompiler "${_IMPORT_PREFIX}/lib/Qt6QmlCompilerd.lib" "${_IMPORT_PREFIX}/bin/Qt6QmlCompilerd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
