#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickDialogs2Utils" for configuration "Debug"
set_property(TARGET Qt6::QuickDialogs2Utils APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickDialogs2Utils PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickDialogs2Utilsd.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Gui"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickDialogs2Utilsd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickDialogs2Utils )
list(APPEND _cmake_import_check_files_for_Qt6::QuickDialogs2Utils "${_IMPORT_PREFIX}/lib/Qt6QuickDialogs2Utilsd.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickDialogs2Utilsd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
