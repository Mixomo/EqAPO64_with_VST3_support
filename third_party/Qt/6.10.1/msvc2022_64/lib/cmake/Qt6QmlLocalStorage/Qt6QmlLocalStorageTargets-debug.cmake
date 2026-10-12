#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QmlLocalStorage" for configuration "Debug"
set_property(TARGET Qt6::QmlLocalStorage APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QmlLocalStorage PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QmlLocalStoraged.lib"
  IMPORTED_LINK_DEPENDENT_LIBRARIES_DEBUG "Qt6::Core;Qt6::Qml"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QmlLocalStoraged.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QmlLocalStorage )
list(APPEND _cmake_import_check_files_for_Qt6::QmlLocalStorage "${_IMPORT_PREFIX}/lib/Qt6QmlLocalStoraged.lib" "${_IMPORT_PREFIX}/bin/Qt6QmlLocalStoraged.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
