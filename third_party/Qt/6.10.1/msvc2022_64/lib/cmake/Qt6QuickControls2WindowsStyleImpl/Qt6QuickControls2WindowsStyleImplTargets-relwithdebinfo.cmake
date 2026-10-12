#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickControls2WindowsStyleImpl" for configuration "RelWithDebInfo"
set_property(TARGET Qt6::QuickControls2WindowsStyleImpl APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(Qt6::QuickControls2WindowsStyleImpl PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/Qt6QuickControls2WindowsStyleImpl.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO "Qt6::Qml;Qt6::Core"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/Qt6QuickControls2WindowsStyleImpl.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickControls2WindowsStyleImpl )
list(APPEND _cmake_import_check_files_for_Qt6::QuickControls2WindowsStyleImpl "${_IMPORT_PREFIX}/lib/Qt6QuickControls2WindowsStyleImpl.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickControls2WindowsStyleImpl.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
