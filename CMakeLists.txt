cmake_minimum_required(VERSION 3.20)
project(meshAssembler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Usage:
# cmake -S . -B build -DMAYA_LOCATION="C:/Program Files/Autodesk/Maya2026"
# cmake --build build --config Release

if(NOT DEFINED MAYA_LOCATION)
    if(DEFINED ENV{MAYA_LOCATION})
        set(MAYA_LOCATION $ENV{MAYA_LOCATION})
    else()
        message(FATAL_ERROR "Set MAYA_LOCATION to your Maya install directory")
    endif()
endif()

if(WIN32)
    set(MAYA_LIB_DIR "${MAYA_LOCATION}/lib")
    set(PLUGIN_EXT ".mll")
elseif(APPLE)
    set(MAYA_LIB_DIR "${MAYA_LOCATION}/Maya.app/Contents/MacOS")
    set(PLUGIN_EXT ".bundle")
else()
    set(MAYA_LIB_DIR "${MAYA_LOCATION}/lib")
    set(PLUGIN_EXT ".so")
endif()

add_library(meshAssembler SHARED
    src/MeshAssemblerNode.cpp
    src/MeshAssemblerCmd.cpp
)

target_include_directories(meshAssembler PRIVATE "${MAYA_LOCATION}/include")

target_link_directories(meshAssembler PRIVATE "${MAYA_LIB_DIR}")

target_link_libraries(meshAssembler PRIVATE OpenMaya Foundation)

set_target_properties(meshAssembler PROPERTIES
    PREFIX ""
    SUFFIX "${PLUGIN_EXT}"
)

if(WIN32)
    target_compile_definitions(meshAssembler PRIVATE NT_PLUGIN REQUIRE_IOSTREAM)
endif()
