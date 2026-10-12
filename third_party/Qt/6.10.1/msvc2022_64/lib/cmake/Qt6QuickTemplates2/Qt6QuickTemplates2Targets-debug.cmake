#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickTemplates2" for configuration "Debug"
set_property(TARGET Qt6::QuickTemplates2 APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickTemplates2 PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickTemplates2d.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Qml"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickTemplates2d.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickTemplates2 )
list(APPEND _cmake_import_check_files_for_Qt6::QuickTemplates2 "${_IMPORT_PREFIX}/lib/Qt6QuickTemplates2d.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickTemplates2d.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
