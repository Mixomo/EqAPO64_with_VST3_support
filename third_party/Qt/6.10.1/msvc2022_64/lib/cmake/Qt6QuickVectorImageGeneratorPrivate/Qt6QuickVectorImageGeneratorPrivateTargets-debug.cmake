#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickVectorImageGeneratorPrivate" for configuration "Debug"
set_property(TARGET Qt6::QuickVectorImageGeneratorPrivate APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickVectorImageGeneratorPrivate PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickVectorImageGeneratord.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Core;Qt6::Quick;Qt6::QuickShapesPrivate;Qt6::Svg"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickVectorImageGeneratord.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickVectorImageGeneratorPrivate )
list(APPEND _cmake_import_check_files_for_Qt6::QuickVectorImageGeneratorPrivate "${_IMPORT_PREFIX}/lib/Qt6QuickVectorImageGeneratord.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickVectorImageGeneratord.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
