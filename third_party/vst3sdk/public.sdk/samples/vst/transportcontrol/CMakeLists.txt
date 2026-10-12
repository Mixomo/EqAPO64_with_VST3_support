
if(NOT SMTG_ENABLE_VSTGUI_SUPPORT)
    return()
endif()

cmake_minimum_required(VERSION 3.25.0)

project(smtg-vst3-transportcontrol
    VERSION ${vstsdk_VERSION}.0
    DESCRIPTION "Steinberg VST 3 Transport Control example"
)

smtg_add_vst3plugin(transport-control 
    include/plugcontroller.h
    include/plugids.h
    include/plugprocessor.h
    include/uitransportcontroller.h
    include/version.h
    source/plugfactory.cpp
    source/plugcontroller.cpp
    source/plugprocessor.cpp
    resource/plug.uidesc
)

target_link_libraries(transport-control 
    PRIVATE
        vstgui_support
)

smtg_target_add_plugin_resources(transport-control
    RESOURCES
        resource/plug.uidesc
)

smtg_target_add_plugin_snapshots(transport-control
    RESOURCES
        resource/5E12B8CB1E924F8E8551BC8C1E312392_snapshot.png
        resource/5E12B8CB1E924F8E8551BC8C1E312392_snapshot_2.0x.png
)

smtg_target_setup_as_vst3_example(transport-control)
