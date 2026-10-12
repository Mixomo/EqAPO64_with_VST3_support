#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickControls2Basic" for configuration "RelWithDebInfo"
set_property(TARGET Qt6::QuickControls2Basic APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(Qt6::QuickControls2Basic PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/Qt6QuickControls2Basic.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO "Qt6::Core;Qt6::Gui;Qt6::Qml;Qt6::QuickControls2BasicStyleImpl;Qt6::QuickControls2;Qt6::Quick;Qt6::QuickTemplates2"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/Qt6QuickControls2Basic.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickControls2Basic )
list(APPEND _cmake_import_check_files_for_Qt6::QuickControls2Basic "${_IMPORT_PREFIX}/lib/Qt6QuickControls2Basic.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickControls2Basic.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
