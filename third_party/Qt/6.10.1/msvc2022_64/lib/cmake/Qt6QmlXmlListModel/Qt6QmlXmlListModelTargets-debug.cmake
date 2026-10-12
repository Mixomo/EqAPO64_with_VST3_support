#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QmlXmlListModel" for configuration "Debug"
set_property(TARGET Qt6::QmlXmlListModel APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QmlXmlListModel PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QmlXmlListModeld.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::QmlModels"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QmlXmlListModeld.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QmlXmlListModel )
list(APPEND _cmake_import_check_files_for_Qt6::QmlXmlListModel "${_IMPORT_PREFIX}/lib/Qt6QmlXmlListModeld.lib" "${_IMPORT_PREFIX}/bin/Qt6QmlXmlListModeld.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
