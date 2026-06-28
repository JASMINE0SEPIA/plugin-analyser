cmake_minimum_required(VERSION 3.22)
project(plugin-analyser VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/EHsc)
endif()

set(JUCE_DIR "~/JUCE")
get_filename_component(JUCE_DIR ${JUCE_DIR} ABSOLUTE)

option(PLUGIN_ANALYSER_USE_FETCH_JUCE "Use FetchContent to get JUCE if local not found" ON)

if(EXISTS ${JUCE_DIR}/CMakeLists.txt)
    message(STATUS "Using local JUCE installation at ${JUCE_DIR}")
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)
    set(JUCE_ENABLE_MODULE_SOURCE_GROUPS OFF)
    set(JUCE_BUILD_EXTRAS OFF)
    set(JUCE_BUILD_EXAMPLES OFF)
elseif(PLUGIN_ANALYSER_USE_FETCH_JUCE)
    message(STATUS "Local JUCE not found at ${JUCE_DIR}, downloading via FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.0
    )
    FetchContent_MakeAvailable(juce)
else()
    find_package(JUCE CONFIG REQUIRED)
endif()

add_executable(plugin_measure_grid_cli src/main_cli.cpp
    src/Config.cpp src/Config.h
    src/PluginLoader.cpp src/PluginLoader.h
    src/SignalGenerator.cpp src/SignalGenerator.h
    src/BucketSpec.cpp src/BucketSpec.h
    src/RunConfig.h src/BlockContext.h src/Analyzer.h
    src/RawCsvAnalyzer.cpp src/RawCsvAnalyzer.h
    src/RmsPeakAnalyzer.cpp src/RmsPeakAnalyzer.h
    src/TransferCurveAnalyzer.cpp src/TransferCurveAnalyzer.h
    src/LinearResponseAnalyzer.cpp src/LinearResponseAnalyzer.h
    src/ThdAnalyzer.cpp src/ThdAnalyzer.h
    src/PhaseAnalyzer.cpp src/PhaseAnalyzer.h
    src/EnvelopeFollowerAnalyzer.cpp src/EnvelopeFollowerAnalyzer.h
    src/TransientAnalyzer.cpp src/TransientAnalyzer.h
    src/MeasurementEngine.cpp src/MeasurementEngine.h
)

target_compile_definitions(plugin_measure_grid_cli
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_PLUGINHOST_VST3=1
)

target_link_libraries(plugin_measure_grid_cli
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_core
)
