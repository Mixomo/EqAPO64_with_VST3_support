#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickDialogs2" for configuration "Debug"
set_property(TARGET Qt6::QuickDialogs2 APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickDialogs2 PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickDialogs2d.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Qml;Qt6::QuickControls2Impl;Qt6::QuickDialogs2Utils;Qt6::QuickDialogs2QuickImpl;Qt6::QuickTemplates2"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickDialogs2d.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickDialogs2 )
list(APPEND _cmake_import_check_files_for_Qt6::QuickDialogs2 "${_IMPORT_PREFIX}/lib/Qt6QuickDialogs2d.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickDialogs2d.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
