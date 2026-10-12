#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::Qml" for configuration "RelWithDebInfo"
set_property(TARGET Qt6::Qml APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(Qt6::Qml PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/Qt6Qml.lib"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/Qt6Qml.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::Qml )
list(APPEND _cmake_import_check_files_for_Qt6::Qml "${_IMPORT_PREFIX}/lib/Qt6Qml.lib" "${_IMPORT_PREFIX}/bin/Qt6Qml.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
