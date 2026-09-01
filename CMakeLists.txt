# Using the same minimum as the godot-cpp project
cmake_minimum_required(VERSION 3.17)

# Silence unused variable warning when specified from toolchain
if(CMAKE_C_COMPILER)
endif()

set(LIBNAME "GFGD") # "The name of the library"
set(GODOT_PROJECT_DIR "project" CACHE STRING "The directory of a Godot project folder")

# Make sure all the dependencies are satisfied
find_package(Python3 3.4 REQUIRED)
find_program(GIT git REQUIRED)

# Ensure godot-cpp submodule has been updated
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/godot-cpp/src")
    message(NOTICE "godot-cpp bindings source not found")
    message(NOTICE "initializing/updating the godot-cpp submodule...")

    # update the c++ bindings submodule to populate it with the necessary source for the library
    execute_process(
        COMMAND git submodule update --init godot-cpp
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
add_subdirectory(godot-cpp SYSTEM)

# This godot-cpp checkout predates the "GodotCPPModule"/target_doc_sources CMake
# helpers and doesn't expose GODOTCPP_SUFFIX/GODOTCPP_PLATFORM target properties,
# so derive the equivalent naming ourselves (mirrors SConstruct's suffix logic).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" GODOTCPP_PLATFORM)

string(TOLOWER "${CMAKE_BUILD_TYPE}" _gfgd_build_type_lower)
if(_gfgd_build_type_lower STREQUAL "debug")
    set(GODOTCPP_TARGET "template_debug")
else()
    set(GODOTCPP_TARGET "template_release")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(GODOTCPP_ARCH "x86_64")
else()
    set(GODOTCPP_ARCH "x86_32")
endif()

set(GODOTCPP_SUFFIX ".${GODOTCPP_PLATFORM}.${GODOTCPP_TARGET}.${GODOTCPP_ARCH}")

# Now we can specify our own project which will inherit any global cmake properties or variables that have been defined.
project(GFGDExtension
    VERSION 1.0
    DESCRIPTION "GFGD - an Unreal Engine inspired game framework for Godot built as a GDExtension."
    LANGUAGES CXX
)

add_library(${LIBNAME} SHARED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

target_sources(${LIBNAME}
    PRIVATE
    ${SOURCES}
)

# NOTE: this godot-cpp checkout has no target_doc_sources() helper to embed
# doc_classes/*.xml at configure time. Class docs are instead compiled in via
# src/gen/doc_data.gen.cpp, which SCons' GodotCPPDocData builder generates and
# which the SOURCES glob above already picks up once it exists on disk.

# src/ is grouped into per-domain folders and includes are written relative to
# src/, so it has to be on the include path - a quoted include no longer resolves
# by sitting next to the including file.
target_include_directories(${LIBNAME} PRIVATE "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(${LIBNAME} PRIVATE godot-cpp)

# Require at least C++17 for this target
set_property(TARGET ${LIBNAME} PROPERTY CXX_STANDARD 17)

set_target_properties(${LIBNAME}
    PROPERTIES
    # The generator expression here prevents msvc from adding a Debug or Release subdir.
    RUNTIME_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"

    PREFIX ""
    OUTPUT_NAME "${LIBNAME}${GODOTCPP_SUFFIX}"
)

set(GODOT_ADDON_DIR "addons/gfgdextension" CACHE STRING "Addon folder inside the Godot project")
set(GODOT_PROJECT_BINARY_DIR "${PROJECT_SOURCE_DIR}/${GODOT_PROJECT_DIR}/${GODOT_ADDON_DIR}/bin/${GODOTCPP_PLATFORM}")

add_custom_command(TARGET ${LIBNAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${LIBNAME}>" "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>"
)
