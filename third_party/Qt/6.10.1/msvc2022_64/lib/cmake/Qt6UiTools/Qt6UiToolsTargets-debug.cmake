#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::UiTools" for configuration "Debug"
set_property(TARGET Qt6::UiTools APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::UiTools PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6UiToolsd.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::OpenGL"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6UiToolsd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::UiTools )
list(APPEND _cmake_import_check_files_for_Qt6::UiTools "${_IMPORT_PREFIX}/lib/Qt6UiToolsd.lib" "${_IMPORT_PREFIX}/bin/Qt6UiToolsd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
