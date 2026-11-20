cmake_minimum_required(VERSION 3.22)

# macOS build settings. These must be set before project().
# Universal binary (Apple Silicon + Intel) so the same build runs on the M4 Mac mini
# and on the Intel Mac. Deployment target 11.0 = macOS Big Sur or newer.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build for Apple Silicon and Intel")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(Pedwar VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PEDWAR_BUILD_PLUGIN "Build the AU/VST3 plugins (needs JUCE)" ON)
option(PEDWAR_BUILD_TESTS  "Build the engine self-tests" ON)
option(PEDWAR_BUILD_CHECK  "Build PedwarCheck, the host-less test of the plugin layer (needs JUCE)" OFF)

if(PEDWAR_BUILD_TESTS)
    enable_testing()
endif()

# 1) The audio engine: plain C++, no plugin framework, reusable for later plugins.
add_subdirectory(engine)

# 2) The two plugin wrappers (PEDWAR and PEDWAR Synth), their controls and the panel (JUCE).
if(PEDWAR_BUILD_PLUGIN OR PEDWAR_BUILD_CHECK)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.17-security
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
    if(PEDWAR_BUILD_PLUGIN)
        add_subdirectory(plugin)
    endif()
    if(PEDWAR_BUILD_CHECK)
        add_subdirectory(check)
    endif()
endif()
