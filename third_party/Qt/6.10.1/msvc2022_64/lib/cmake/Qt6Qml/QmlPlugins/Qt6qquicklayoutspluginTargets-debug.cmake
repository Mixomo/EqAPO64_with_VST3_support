#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::qquicklayoutsplugin" for configuration "Debug"
set_property(TARGET Qt6::qquicklayoutsplugin APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::qquicklayoutsplugin PROPERTIES
  IMPORTED_COMMON_LANGUAGE_RUNTIME_DEBUG ""
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/qml/QtQuick/Layouts/qquicklayoutsplugind.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::qquicklayoutsplugin )
list(APPEND _cmake_import_check_files_for_Qt6::qquicklayoutsplugin "${_IMPORT_PREFIX}/qml/QtQuick/Layouts/qquicklayoutsplugind.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
