#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QuickShapesDesignHelpersPrivate" for configuration "Debug"
set_property(TARGET Qt6::QuickShapesDesignHelpersPrivate APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QuickShapesDesignHelpersPrivate PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/Qt6QuickShapesDesignHelpersd.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/Qt6QuickShapesDesignHelpersd.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QuickShapesDesignHelpersPrivate )
list(APPEND _cmake_import_check_files_for_Qt6::QuickShapesDesignHelpersPrivate "${_IMPORT_PREFIX}/lib/Qt6QuickShapesDesignHelpersd.lib" "${_IMPORT_PREFIX}/bin/Qt6QuickShapesDesignHelpersd.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
