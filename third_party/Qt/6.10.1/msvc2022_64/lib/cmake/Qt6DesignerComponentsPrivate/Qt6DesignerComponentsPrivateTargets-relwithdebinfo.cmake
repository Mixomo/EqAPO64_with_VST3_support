#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::DesignerComponentsPrivate" for configuration "RelWithDebInfo"
set_property(TARGET Qt6::DesignerComponentsPrivate APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(Qt6::DesignerComponentsPrivate PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/Qt6DesignerComponents.lib"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/Qt6DesignerComponents.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::DesignerComponentsPrivate )
list(APPEND _cmake_import_check_files_for_Qt6::DesignerComponentsPrivate "${_IMPORT_PREFIX}/lib/Qt6DesignerComponents.lib" "${_IMPORT_PREFIX}/bin/Qt6DesignerComponents.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
