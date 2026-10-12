
if(SMTG_WIN)
    option(SMTG_PLUGIN_TARGET_USER_PROGRAM_FILES_COMMON "use FOLDERID_UserProgramFilesCommon as VST3 target path" ON)
endif(SMTG_WIN)

#------------------------------------------------------------------------
# VST3 Folder (following the specification)
function(smtg_get_default_vst3_path)
    if(SMTG_WIN)
        set(win64Found "1")

        # --- Case 1: A platform was explicitly specified (Visual Studio) ---
        if(DEFINED CMAKE_GENERATOR_PLATFORM AND CMAKE_GENERATOR_PLATFORM)
        
            # Normalize for safety
            string(TOUPPER "${CMAKE_GENERATOR_PLATFORM}" _plat)

            # Look for x64 or ARM64
            string(FIND "${_plat}" "X64" win64Found)
            if(${win64Found} EQUAL -1)
                string(FIND "${_plat}" "ARM64" win64Found)
            endif()

        # --- Case 2: Old Visual Studio generators without platform support ---
        elseif(${CMAKE_GENERATOR} STREQUAL "Visual Studio 15 2017" OR 
               ${CMAKE_GENERATOR} STREQUAL "Visual Studio 14 2015" OR 
               ${CMAKE_GENERATOR} STREQUAL "Visual Studio 12 2013" OR 
               ${CMAKE_GENERATOR} STREQUAL "Visual Studio 11 2012" OR 
               ${CMAKE_GENERATOR} STREQUAL "Visual Studio 10 2010" OR 
               ${CMAKE_GENERATOR} STREQUAL "Visual Studio 9 2008")
            # These generators default to Win32 unless explicitly configured.
            # So: NOT 64-bit => mark NOT found
            set(win64Found "-1")

        # --- Case 3: Other generators (Ninja, Makefiles, etc.) ---
        else()
            string(FIND "${CMAKE_GENERATOR}" "Win32" win32Found)
            if(NOT ${win32Found} EQUAL -1)
                set(win64Found "-1")
            endif()
        endif()

        # --- for 32bits OS
        if(${win64Found} EQUAL -1)
            if(EXISTS "$ENV{PROGRAMFILES} (x86)")
                set(SMTG_PLUGIN_TARGET_DEFAULT_PATH "$ENV{PROGRAMFILES} (x86)\\Common Files\\VST3" PARENT_SCOPE)
            else()
                set(SMTG_PLUGIN_TARGET_DEFAULT_PATH "$ENV{PROGRAMFILES}\\Common Files\\VST3" PARENT_SCOPE)
            endif()
        # --- for 64bits OS
        else()
            #FOLDERID_UserProgramFilesCommon
            if(SMTG_PLUGIN_TARGET_USER_PROGRAM_FILES_COMMON)
                set(SMTG_PLUGIN_TARGET_DEFAULT_PATH "$ENV{LOCALAPPDATA}\\Programs\\Common\\VST3" PARENT_SCOPE)
            else()
                set(SMTG_PLUGIN_TARGET_DEFAULT_PATH "$ENV{CommonProgramW6432}\\VST3" PARENT_SCOPE)
            endif(SMTG_PLUGIN_TARGET_USER_PROGRAM_FILES_COMMON)
        endif()
    elseif(SMTG_MAC)
        set(SMTG_PLUGIN_TARGET_DEFAULT_PATH "$ENV{HOME}/Library/Audio/Plug-Ins/VST3" PARENT_SCOPE)
    elseif(SMTG_LINUX)
        set(SMTG_PLUGIN_TARGET_DEFAULT_PATH "$ENV{HOME}/.vst3" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "[SMTG] unknown platform")
    endif(SMTG_WIN)
endfunction(smtg_get_default_vst3_path)
