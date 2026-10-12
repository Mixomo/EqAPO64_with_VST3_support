#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::Designer" for configuration "RelWithDebInfo"
set_property(TARGET Qt6::Designer APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(Qt6::Designer PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/Qt6Designer.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO "Qt6::OpenGL"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/Qt6Designer.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::Designer )
list(APPEND _cmake_import_check_files_for_Qt6::Designer "${_IMPORT_PREFIX}/lib/Qt6Designer.lib" "${_IMPORT_PREFIX}/bin/Qt6Designer.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
