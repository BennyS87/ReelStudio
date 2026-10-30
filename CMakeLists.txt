cmake_minimum_required(VERSION 3.22)
project(ReelStudio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- Dependencies: JUCE 8 and Tracktion Engine 3.2, downloaded automatically on first configure ----
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(juce
    URL https://github.com/juce-framework/JUCE/archive/19edd538429c93d277bf95b55aaa7e3eb545f951.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(tracktion
    URL https://github.com/Tracktion/tracktion_engine/archive/refs/tags/v3.2.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR modules)
FetchContent_MakeAvailable(juce)
FetchContent_MakeAvailable(tracktion)

# ---- The app ----
juce_add_gui_app(ReelStudio
    PRODUCT_NAME "Reel Studio"
    COMPANY_NAME "Reel"
    BUNDLE_ID "com.reel.studio"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Reel Studio uses audio input only if you choose to record."
    DOCUMENT_EXTENSIONS reel)

file(GLOB REEL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(ReelStudio PRIVATE ${REEL_SOURCES})
target_compile_features(ReelStudio PRIVATE cxx_std_20)
set_target_properties(ReelStudio PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)

target_compile_definitions(ReelStudio PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ReelStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ReelStudio,JUCE_VERSION>"
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_VST3=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries(ReelStudio PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)

if(UNIX AND NOT APPLE)
    target_link_libraries(ReelStudio PRIVATE "-latomic")
endif()
