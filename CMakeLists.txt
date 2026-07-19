cmake_minimum_required(VERSION 3.22)

if(NOT "${METAMODULE_SDK_DIR}" STREQUAL "")
    message("METAMODULE_SDK_DIR set by CMake variable ${METAMODULE_SDK_DIR}")
elseif(DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
    message("METAMODULE_SDK_DIR set by environment variable ${METAMODULE_SDK_DIR}")
else()
    set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/metamodule-plugin-sdk")
    message("METAMODULE_SDK_DIR set to default: ${METAMODULE_SDK_DIR}")
endif()

include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(SignalFunctionSet
    VERSION 1.4.2
    DESCRIPTION "Signal Function Set plugin for MetaModule"
    LANGUAGES C CXX ASM
)

# Override at configure time with -DPLUGIN_VERSION=v0.X.Y if needed
if(NOT DEFINED PLUGIN_VERSION)
    set(PLUGIN_VERSION "v${PROJECT_VERSION}")
endif()

add_library(SignalFunctionSet STATIC)

set(SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})

target_sources(SignalFunctionSet PRIVATE
    ${SOURCE_DIR}/src/plugin.cpp
    ${SOURCE_DIR}/src/quadlfo.cpp
    ${SOURCE_DIR}/src/gsx.cpp
    ${SOURCE_DIR}/src/fugue.cpp
    ${SOURCE_DIR}/src/phase.cpp
    ${SOURCE_DIR}/src/overtone.cpp
    ${SOURCE_DIR}/src/intone.cpp
    ${SOURCE_DIR}/src/tine.cpp
    ${SOURCE_DIR}/src/meter.cpp
    ${SOURCE_DIR}/src/swell.cpp
    ${SOURCE_DIR}/src/shift.cpp
    ${SOURCE_DIR}/src/vac.cpp
    ${SOURCE_DIR}/src/band.cpp
    ${SOURCE_DIR}/src/metafugue.cpp
    ${SOURCE_DIR}/src/muse.cpp
    ${SOURCE_DIR}/src/cycle.cpp
    ${SOURCE_DIR}/src/chance.cpp
    # Operator (Bell) + OP ENV (OpEnv) + vendored msfa DX7 engine
    ${SOURCE_DIR}/src/bell.cpp
    ${SOURCE_DIR}/src/bell_voice.cpp
    ${SOURCE_DIR}/src/op_env.cpp
    ${SOURCE_DIR}/src/op_env_engine.cpp
    ${SOURCE_DIR}/src/msfa/dx7note.cc
    ${SOURCE_DIR}/src/msfa/env.cc
    ${SOURCE_DIR}/src/msfa/exp2.cc
    ${SOURCE_DIR}/src/msfa/fm_core.cc
    ${SOURCE_DIR}/src/msfa/fm_op_kernel.cc
    ${SOURCE_DIR}/src/msfa/freqlut.cc
    ${SOURCE_DIR}/src/msfa/lfo.cc
    ${SOURCE_DIR}/src/msfa/log2.cc
    ${SOURCE_DIR}/src/msfa/patch.cc
    ${SOURCE_DIR}/src/msfa/pitchenv.cc
    ${SOURCE_DIR}/src/msfa/sin.cc
    # Sequencers whose editing lives on the display — patterns can be built in
    # VCV and played back on MetaModule.
    ${SOURCE_DIR}/src/beat.cpp
    ${SOURCE_DIR}/src/note.cpp
    # New: song-form arrangement sequencer + auto-sampler + poly SFZ player.
    ${SOURCE_DIR}/src/arrange.cpp
    ${SOURCE_DIR}/src/record.cpp
    ${SOURCE_DIR}/src/play.cpp
)

target_include_directories(SignalFunctionSet PRIVATE
    ${SOURCE_DIR}/src
)

# The vendored msfa DX7 engine was written for desktop compilers and mixes
# `int*` with `int32_t*`. On MetaModule's ARM toolchain int32_t is `long int`,
# so the strict type checker rejects calls that desktop compilers waved
# through. -fpermissive downgrades those mismatches to warnings; the
# code is correct on either ABI because the pointed-to values are still
# 32-bit signed integers.
set_source_files_properties(
    ${SOURCE_DIR}/src/msfa/fm_op_kernel.cc
    ${SOURCE_DIR}/src/msfa/dx7note.cc
    ${SOURCE_DIR}/src/msfa/fm_core.cc
    ${SOURCE_DIR}/src/msfa/env.cc
    ${SOURCE_DIR}/src/msfa/lfo.cc
    ${SOURCE_DIR}/src/msfa/pitchenv.cc
    ${SOURCE_DIR}/src/msfa/patch.cc
    PROPERTIES COMPILE_FLAGS "-fpermissive -Wno-narrowing"
)

target_compile_definitions(SignalFunctionSet PRIVATE
    METAMODULE
)

if("${INSTALL_DIR}" STREQUAL "")
    set(INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins)
endif()

create_plugin(
    SOURCE_LIB      SignalFunctionSet
    PLUGIN_NAME      SignalFunctionSet
    PLUGIN_JSON      ${SOURCE_DIR}/plugin.json
    SOURCE_ASSETS    ${SOURCE_DIR}/assets
    DESTINATION      ${INSTALL_DIR}
)

# After every build, also produce a properly-named versioned copy
# (matches the format that the MetaModule plugin scanner / firmware expect:
#  SignalFunctionSet-v0.1.2.mmplugin)
add_custom_command(
    TARGET plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${INSTALL_DIR}/SignalFunctionSet.mmplugin
        ${INSTALL_DIR}/SignalFunctionSet-${PLUGIN_VERSION}.mmplugin
    COMMENT "Local deploy: SignalFunctionSet-${PLUGIN_VERSION}.mmplugin"
)
