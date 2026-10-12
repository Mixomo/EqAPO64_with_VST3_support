#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QmlXmlListModel" for configuration "RelWithDebInfo"
set_property(TARGET Qt6::QmlXmlListModel APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(Qt6::QmlXmlListModel PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/Qt6QmlXmlListModel.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO "Qt6::QmlModels"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/Qt6QmlXmlListModel.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QmlXmlListModel )
list(APPEND _cmake_import_check_files_for_Qt6::QmlXmlListModel "${_IMPORT_PREFIX}/lib/Qt6QmlXmlListModel.lib" "${_IMPORT_PREFIX}/bin/Qt6QmlXmlListModel.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
