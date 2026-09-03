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

set(GODOTCPP_DEFAULT_API_VERSION "4.7")
add_subdirectory(godot-cpp SYSTEM)

# When compiling for emscripten, this toolchainhack needs to be included.
# set(CMAKE_PROJECT_<project-name>_INCLUDE ${godot-cpp_SOURCE_DIR}/cmake/emsdkHack.cmake)

# Create the gen directory at configure time so IDEs can see generated headers immediately after generate_bindings
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/godot-cpp/gen/include")

# Add godot-cpp's module path and include the exported functions.
# This is made available for documentation generation
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${godot-cpp_SOURCE_DIR}/cmake")
include(GodotCPPModule)

# The godot-cpp target has some of useful properties attached that can be retrieved like so.
get_target_property(GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)
get_target_property(GODOTCPP_PLATFORM godot::cpp GODOTCPP_PLATFORM)

# ".universal" just means "compatible with all relevant arches", so a macOS
# universal build carries no architecture in its name - the same trim SConstruct
# does. GODOTCPP_SUFFIX is a string of generator expressions, and the arch sits
# in a literal "$<1:.universal>", so replacing the text leaves a valid "$<1:>".
string(REPLACE ".universal" "" GODOTCPP_SUFFIX "${GODOTCPP_SUFFIX}")

# Now we can specify our own project which will inherit any global cmake properties or variables that have been defined.
project(GFGDExtension
    VERSION 1.0
    DESCRIPTION "GFGD - an Unreal Engine inspired game framework for Godot built as a GDExtension."
    HOMEPAGE_URL "https://github.com/Drakeblood/GFGDExtension"
    LANGUAGES CXX
)

add_library(${LIBNAME} SHARED)

# Sources are grouped into per-domain folders under src/, so collect them
# recursively. src/gen is skipped: that is where SCons writes doc_data.gen.cpp,
# and target_doc_sources() below generates the CMake equivalent into the build
# tree - compiling both would register the class docs twice.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.h"
)
list(FILTER SOURCES EXCLUDE REGEX "/src/gen/")

target_sources(${LIBNAME}
    PRIVATE
        ${SOURCES}
)

# Includes are written relative to src/, so it has to be on the include path -
# a quoted include no longer resolves by sitting next to the including file.
target_include_directories(${LIBNAME} PRIVATE "${PROJECT_SOURCE_DIR}/src")

# Fetch a list of the xml files to use for documentation and add to our target
file(GLOB_RECURSE DOC_XML LIST_DIRECTORIES NO CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/doc_classes/*.xml")

# conditionally add doc data to compile output
if(DOC_XML)
    if(GODOTCPP_TARGET MATCHES "editor|template_debug")
        target_doc_sources(${LIBNAME} "${DOC_XML}")
    endif()
endif()

target_link_libraries(${LIBNAME} PRIVATE godot-cpp)

# godot-cpp only defines TOOLS_ENABLED from its SCons build (tools/godotcpp.py);
# the CMake build leaves it out entirely. Without it an editor build compiles
# away every #ifdef TOOLS_ENABLED block - that is the whole of src/editor,
# including the EditorPlugin the gameplay tag inspectors are registered from -
# and the resulting library is a template_debug build under another name.
if(GODOTCPP_TARGET STREQUAL "editor")
    target_compile_definitions(${LIBNAME} PRIVATE TOOLS_ENABLED)
endif()

# Require at least C++17 for this target
set_property(TARGET ${LIBNAME} PROPERTY CXX_STANDARD 17)

# gfgd.gdextension names the Windows libraries bare and every other platform
# with the usual "lib". PREFIX is resolved at configure time and takes no
# generator expression, so this has to branch on CMAKE_SYSTEM_NAME - which is
# only set for this scope once project() above has run, not by godot-cpp's own
# project() call inside the subdirectory.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(GFGD_LIB_PREFIX "")
else()
    set(GFGD_LIB_PREFIX "lib")
endif()

set_target_properties(${LIBNAME}
    PROPERTIES
    # The generator expression here prevents msvc from adding a Debug or Release subdir.
    # A shared library is a RUNTIME target only on Windows; everywhere else the
    # .so/.dylib is a LIBRARY target, so both have to be pointed at bin/.
    LIBRARY_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"

    PREFIX "${GFGD_LIB_PREFIX}"
    OUTPUT_NAME "${LIBNAME}${GODOTCPP_SUFFIX}"
)

# The addon folder is self-contained: gfgd.gdextension names its libraries with
# paths relative to itself, so the build output has to land under the addon, not
# in the project root.
set(GODOT_ADDON_DIR "addons/gfgdextension" CACHE STRING "Addon folder inside the Godot project")
set(GODOT_PROJECT_BINARY_DIR "${PROJECT_SOURCE_DIR}/${GODOT_PROJECT_DIR}/${GODOT_ADDON_DIR}/bin/${GODOTCPP_PLATFORM}")

add_custom_command(TARGET ${LIBNAME} POST_BUILD
    # Only the platforms already in use have a folder checked in, so ios and web
    # would otherwise fail the copy on a clean checkout.
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GODOT_PROJECT_BINARY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${LIBNAME}>" "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>"
)
