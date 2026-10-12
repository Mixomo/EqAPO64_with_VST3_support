#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickControls2MaterialStyleImpl" for configuration "RelWithDebInfo"
set_property(TARGET Qt6::QuickControls2MaterialStyleImpl APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(Qt6::QuickControls2MaterialStyleImpl PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/Qt6QuickControls2MaterialStyleImpl.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO "Qt6::Core;Qt6::Gui;Qt6::Qml;Qt6::QuickControls2Impl;Qt6::Quick;Qt6::QuickTemplates2"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/Qt6QuickControls2MaterialStyleImpl.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickControls2MaterialStyleImpl )
list(APPEND _cmake_import_check_files_for_Qt6::QuickControls2MaterialStyleImpl "${_IMPORT_PREFIX}/lib/Qt6QuickControls2MaterialStyleImpl.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickControls2MaterialStyleImpl.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
