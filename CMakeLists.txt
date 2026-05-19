cmake_minimum_required(VERSION 3.22)
project(PerformanceDAW VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ARM64 only — M4 Pro target
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)

# tracktion_engine vendors JUCE as a submodule.
# Set TRACKTION_JUCE_PATH if you want to use a separate JUCE checkout instead.
# Disable the TE example projects — they add build time and an LV2 crash on macOS.
set(TRACKTION_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TRACKTION_BUILD_DEMO     OFF CACHE BOOL "" FORCE)
add_subdirectory(tracktion_engine)

# ── App target ────────────────────────────────────────────────────────────────
juce_add_gui_app(PerformanceDAW
    PRODUCT_NAME                    "PerformanceDAW"
    BUNDLE_ID                       "com.zerowake.performancedaw"
    COMPANY_NAME                    "Zero Wake"
    MICROPHONE_PERMISSION_ENABLED   TRUE
    AUDIO_DEVICE_PERMISSION_ENABLED TRUE
)

# Generates JuceHeader.h and adds the artefacts dir to the include path.
juce_generate_juce_header(PerformanceDAW)

target_sources(PerformanceDAW PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Engine/PlaybackEngine.cpp
    Source/UI/TransportComponent.cpp
    Source/UI/SetlistComponent.cpp
    Source/UI/TimelineComponent.cpp
    Source/UI/InspectorComponent.cpp
)


target_include_directories(PerformanceDAW PRIVATE Source)

target_compile_definitions(PerformanceDAW PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PerformanceDAW,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PerformanceDAW,JUCE_VERSION>"
)

target_link_libraries(PerformanceDAW PRIVATE
    tracktion_engine            # JUCE module — exposes te:: namespace
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
