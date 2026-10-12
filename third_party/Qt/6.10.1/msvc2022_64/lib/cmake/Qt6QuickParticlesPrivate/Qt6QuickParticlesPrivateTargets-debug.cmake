#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickParticlesPrivate" for configuration "Debug"
set_property(TARGET Qt6::QuickParticlesPrivate APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickParticlesPrivate PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickParticlesd.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Qml"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickParticlesd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickParticlesPrivate )
list(APPEND _cmake_import_check_files_for_Qt6::QuickParticlesPrivate "${_IMPORT_PREFIX}/lib/Qt6QuickParticlesd.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickParticlesd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
