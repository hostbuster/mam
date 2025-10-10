cmake_minimum_required(VERSION 3.15)

project(mam LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    target_compile_options(mam PRIVATE -Wall -Wextra -Wpedantic)
endif()


