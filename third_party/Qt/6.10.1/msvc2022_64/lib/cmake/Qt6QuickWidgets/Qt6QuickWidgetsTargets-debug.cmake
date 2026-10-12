#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickWidgets" for configuration "Debug"
set_property(TARGET Qt6::QuickWidgets APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickWidgets PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickWidgetsd.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickWidgetsd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickWidgets )
list(APPEND _cmake_import_check_files_for_Qt6::QuickWidgets "${_IMPORT_PREFIX}/lib/Qt6QuickWidgetsd.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickWidgetsd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
