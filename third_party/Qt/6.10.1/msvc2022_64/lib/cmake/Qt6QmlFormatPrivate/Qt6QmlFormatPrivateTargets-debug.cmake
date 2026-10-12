#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QmlFormatPrivate" for configuration "Debug"
set_property(TARGET Qt6::QmlFormatPrivate APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QmlFormatPrivate PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QmlFormatd.lib"
  )

list(APPEND _cmake_import_check_targets Qt6::QmlFormatPrivate )
list(APPEND _cmake_import_check_files_for_Qt6::QmlFormatPrivate "${_IMPORT_PREFIX}/lib/Qt6QmlFormatd.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
