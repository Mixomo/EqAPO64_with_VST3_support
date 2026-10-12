#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::LabsQmlModels" for configuration "Debug"
set_property(TARGET Qt6::LabsQmlModels APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::LabsQmlModels PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6LabsQmlModelsd.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::QmlModels;Qt6::Qml"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6LabsQmlModelsd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::LabsQmlModels )
list(APPEND _cmake_import_check_files_for_Qt6::LabsQmlModels "${_IMPORT_PREFIX}/lib/Qt6LabsQmlModelsd.lib" "${_IMPORT_PREFIX}/bin/Qt6LabsQmlModelsd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
