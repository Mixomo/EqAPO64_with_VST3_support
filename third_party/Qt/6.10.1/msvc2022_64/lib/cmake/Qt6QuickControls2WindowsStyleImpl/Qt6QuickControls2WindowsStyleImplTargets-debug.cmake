#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickControls2WindowsStyleImpl" for configuration "Debug"
set_property(TARGET Qt6::QuickControls2WindowsStyleImpl APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickControls2WindowsStyleImpl PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickControls2WindowsStyleImpld.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Qml;Qt6::Core"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickControls2WindowsStyleImpld.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickControls2WindowsStyleImpl )
list(APPEND _cmake_import_check_files_for_Qt6::QuickControls2WindowsStyleImpl "${_IMPORT_PREFIX}/lib/Qt6QuickControls2WindowsStyleImpld.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickControls2WindowsStyleImpld.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
