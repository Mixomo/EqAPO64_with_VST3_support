#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QmlCore" for configuration "Debug"
set_property(TARGET Qt6::QmlCore APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QmlCore PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QmlCored.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QmlCored.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QmlCore )
list(APPEND _cmake_import_check_files_for_Qt6::QmlCore "${_IMPORT_PREFIX}/lib/Qt6QmlCored.lib" "${_IMPORT_PREFIX}/bin/Qt6QmlCored.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
