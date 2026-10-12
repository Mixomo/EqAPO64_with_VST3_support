#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "MPG123::libmpg123" for configuration "Release"
set_property(TARGET MPG123::libmpg123 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(MPG123::libmpg123 PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/mpg123.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/mpg123.dll"
  )

list(APPEND _cmake_import_check_targets MPG123::libmpg123 )
list(APPEND _cmake_import_check_files_for_MPG123::libmpg123 "${_IMPORT_PREFIX}/lib/mpg123.lib" "${_IMPORT_PREFIX}/bin/mpg123.dll" )

# Import target "MPG123::libout123" for configuration "Release"
set_property(TARGET MPG123::libout123 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(MPG123::libout123 PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/out123.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/out123.dll"
  )

list(APPEND _cmake_import_check_targets MPG123::libout123 )
list(APPEND _cmake_import_check_files_for_MPG123::libout123 "${_IMPORT_PREFIX}/lib/out123.lib" "${_IMPORT_PREFIX}/bin/out123.dll" )

# Import target "MPG123::libsyn123" for configuration "Release"
set_property(TARGET MPG123::libsyn123 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(MPG123::libsyn123 PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/syn123.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/syn123.dll"
  )

list(APPEND _cmake_import_check_targets MPG123::libsyn123 )
list(APPEND _cmake_import_check_files_for_MPG123::libsyn123 "${_IMPORT_PREFIX}/lib/syn123.lib" "${_IMPORT_PREFIX}/bin/syn123.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
