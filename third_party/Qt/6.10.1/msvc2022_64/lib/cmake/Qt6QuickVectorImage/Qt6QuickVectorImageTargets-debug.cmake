#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickVectorImage" for configuration "Debug"
set_property(TARGET Qt6::QuickVectorImage APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickVectorImage PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickVectorImaged.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Quick;Qt6::QuickVectorImageGeneratorPrivate;Qt6::Svg;Qt6::Qml"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickVectorImaged.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickVectorImage )
list(APPEND _cmake_import_check_files_for_Qt6::QuickVectorImage "${_IMPORT_PREFIX}/lib/Qt6QuickVectorImaged.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickVectorImaged.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
