#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::OpenGL" for configuration "Debug"
set_property(TARGET Qt6::OpenGL APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::OpenGL PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6OpenGLd.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6OpenGLd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::OpenGL )
list(APPEND _cmake_import_check_files_for_Qt6::OpenGL "${_IMPORT_PREFIX}/lib/Qt6OpenGLd.lib" "${_IMPORT_PREFIX}/bin/Qt6OpenGLd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
