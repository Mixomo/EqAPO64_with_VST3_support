#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickVectorImageHelpers" for configuration "Debug"
set_property(TARGET Qt6::QuickVectorImageHelpers APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickVectorImageHelpers PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickVectorImageHelpersd.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Quick;Qt6::QuickVectorImageGeneratorPrivate;Qt6::Qml"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickVectorImageHelpersd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickVectorImageHelpers )
list(APPEND _cmake_import_check_files_for_Qt6::QuickVectorImageHelpers "${_IMPORT_PREFIX}/lib/Qt6QuickVectorImageHelpersd.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickVectorImageHelpersd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
