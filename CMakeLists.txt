cmake_minimum_required(VERSION 3.15)

project(mam LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MAM_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(MAM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(MAM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(MAM_ENABLE_LTO "Enable Link-Time Optimization" OFF)
option(MAM_USE_JSON_SCHEMA "Enable strict JSON Schema validation (requires json-schema.hpp)" OFF)

include_directories(third_party)


add_library(mam_core STATIC
    src/core/Instrument.hpp
    src/core/Node.hpp
    src/core/NodeFactory.hpp
    src/core/MixerNode.hpp
    src/core/GraphConfig.hpp
    src/core/GraphConfig.cpp
    src/core/Graph.hpp
    src/core/AudioBuffer.hpp
    src/core/Command.hpp
    src/core/JobPool.hpp
    src/core/ParamIds.hpp
    src/core/ParameterRegistry.hpp
    src/core/ParamMap.hpp
    src/core/TransportNode.hpp
    src/core/CompressorNode.hpp
    src/core/ReverbNode.hpp
    src/core/WiretapNode.hpp
    src/core/SchemaValidate.hpp
    src/core/SchemaValidate.cpp
)

if (MAM_USE_JSON_SCHEMA)
    target_compile_definitions(mam_core PRIVATE USE_JSON_SCHEMA)
    # If you vendor pboettch/json-schema-validator headers under third_party/json-schema-validator,
    # the global include_directories(third_party) above will make <json-schema.hpp> resolvable.
endif()

add_library(mam_dsp STATIC
    src/instruments/kick/KickSynth.hpp
    src/instruments/kick/KickSynth.cpp
    src/instruments/clap/ClapSynth.hpp
    src/instruments/clap/ClapSynth.cpp
)

add_library(mam_io STATIC
    src/io/AudioFileWriter.hpp
    src/io/AudioFileWriter.cpp
)

add_library(mam_render INTERFACE)
add_executable(mam
    src/main.cpp
)

target_link_libraries(mam_render INTERFACE mam_core mam_dsp mam_io)
target_link_libraries(mam PUBLIC mam_render)

if(APPLE)
    target_compile_definitions(mam PRIVATE MACOSX)
    target_link_libraries(mam PRIVATE
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework CoreServices"
    )
endif()

add_executable(gen_params tools/gen_params.cpp)
target_link_libraries(gen_params PRIVATE mam_core)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/docs/ParamTables.md
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/docs
  COMMAND gen_params > ${CMAKE_CURRENT_SOURCE_DIR}/docs/ParamTables.md
  DEPENDS gen_params src/core/ParamMap.hpp
  COMMENT "Generating docs/ParamTables.md from ParamMap.hpp"
)

add_custom_target(docs ALL DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/docs/ParamTables.md)

foreach(tgt IN ITEMS mam_core mam_dsp mam_io mam)
    if (MSVC)
        target_compile_options(${tgt} PRIVATE /W4)
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if (MAM_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE -Werror)
        endif()
    endif()
endforeach()

if (MAM_ENABLE_ASAN)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        foreach(tgt IN ITEMS mam_core mam_dsp mam_io mam)
            target_compile_options(${tgt} PRIVATE -fsanitize=address)
            target_link_options(${tgt} PRIVATE -fsanitize=address)
        endforeach()
    endif()
endif()

if (MAM_ENABLE_UBSAN)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        foreach(tgt IN ITEMS mam_core mam_dsp mam_io mam)
            target_compile_options(${tgt} PRIVATE -fsanitize=undefined)
            target_link_options(${tgt} PRIVATE -fsanitize=undefined)
        endforeach()
    endif()
endif()

if (MAM_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        foreach(tgt IN ITEMS mam_core mam_dsp mam_io mam)
            set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        endforeach()
    endif()
endif()


