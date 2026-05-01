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
    VERSION 1.0.2
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
)

target_include_directories(SignalFunctionSet PRIVATE
    ${SOURCE_DIR}/src
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
