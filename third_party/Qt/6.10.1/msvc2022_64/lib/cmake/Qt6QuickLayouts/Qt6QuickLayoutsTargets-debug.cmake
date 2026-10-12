#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickLayouts" for configuration "Debug"
set_property(TARGET Qt6::QuickLayouts APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickLayouts PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickLayoutsd.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Gui;Qt6::Quick"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickLayoutsd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickLayouts )
list(APPEND _cmake_import_check_files_for_Qt6::QuickLayouts "${_IMPORT_PREFIX}/lib/Qt6QuickLayoutsd.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickLayoutsd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
