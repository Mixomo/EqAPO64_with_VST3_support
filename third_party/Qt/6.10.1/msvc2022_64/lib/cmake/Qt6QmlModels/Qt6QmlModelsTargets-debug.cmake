#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QmlModels" for configuration "Debug"
set_property(TARGET Qt6::QmlModels APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QmlModels PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QmlModelsd.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QmlModelsd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QmlModels )
list(APPEND _cmake_import_check_files_for_Qt6::QmlModels "${_IMPORT_PREFIX}/lib/Qt6QmlModelsd.lib" "${_IMPORT_PREFIX}/bin/Qt6QmlModelsd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
