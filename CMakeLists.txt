cmake_minimum_required(VERSION 3.15)

project(mam LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MAM_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(MAM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(MAM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(MAM_ENABLE_LTO "Enable Link-Time Optimization" OFF)

include_directories(third_party)

add_executable(mam
    src/main.cpp
    src/core/Instrument.hpp
    src/core/Node.hpp
    src/core/NodeFactory.hpp
    src/core/MixerNode.hpp
    src/core/GraphConfig.hpp
    src/core/GraphConfig.cpp
    src/core/Graph.hpp
    src/core/AudioBuffer.hpp
    src/instruments/kick/KickSynth.hpp
    src/instruments/kick/KickSynth.cpp
    src/instruments/kick/KickNode.hpp
    src/instruments/kick/KickFactory.hpp
    src/instruments/clap/ClapSynth.hpp
    src/instruments/clap/ClapSynth.cpp
    src/instruments/clap/ClapNode.hpp
    src/instruments/clap/ClapFactory.hpp
    src/offline/OfflineRenderer.hpp
    src/offline/OfflineGraphRenderer.hpp
    src/io/AudioFileWriter.hpp
    src/io/AudioFileWriter.cpp
    src/realtime/RealtimeRenderer.hpp
    src/realtime/RealtimeGraphRenderer.hpp
)

if(APPLE)
    target_compile_definitions(mam PRIVATE MACOSX)
    target_link_libraries(mam
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework CoreServices"
    )
endif()

if (MSVC)
    target_compile_options(mam PRIVATE /W4)
else()
    target_compile_options(mam PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if (MAM_WARNINGS_AS_ERRORS)
        target_compile_options(mam PRIVATE -Werror)
    endif()
endif()

if (MAM_ENABLE_ASAN)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(mam PRIVATE -fsanitize=address)
        target_link_options(mam PRIVATE -fsanitize=address)
    endif()
endif()

if (MAM_ENABLE_UBSAN)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(mam PRIVATE -fsanitize=undefined)
        target_link_options(mam PRIVATE -fsanitize=undefined)
    endif()
endif()

if (MAM_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_property(TARGET mam PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()


