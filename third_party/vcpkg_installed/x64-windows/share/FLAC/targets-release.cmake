#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "FLAC::FLAC" for configuration "Release"
set_property(TARGET FLAC::FLAC APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(FLAC::FLAC PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/FLAC.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/FLAC.dll"
  )

list(APPEND _cmake_import_check_targets FLAC::FLAC )
list(APPEND _cmake_import_check_files_for_FLAC::FLAC "${_IMPORT_PREFIX}/lib/FLAC.lib" "${_IMPORT_PREFIX}/bin/FLAC.dll" )

# Import target "FLAC::FLAC++" for configuration "Release"
set_property(TARGET FLAC::FLAC++ APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(FLAC::FLAC++ PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/FLAC++.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/FLAC++.dll"
  )

list(APPEND _cmake_import_check_targets FLAC::FLAC++ )
list(APPEND _cmake_import_check_files_for_FLAC::FLAC++ "${_IMPORT_PREFIX}/lib/FLAC++.lib" "${_IMPORT_PREFIX}/bin/FLAC++.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
