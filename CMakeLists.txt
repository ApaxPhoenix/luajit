cmake_minimum_required(VERSION 3.25)

project(luajit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

get_property(multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT multi AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "The configuration to build" FORCE)
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(source "${CMAKE_CURRENT_SOURCE_DIR}")
set(binary "${CMAKE_CURRENT_BINARY_DIR}")
set(vendors "${source}/vendors")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${source}/bin")

if(MSVC)
    set(warnings /W4 /permissive-)
else()
    set(warnings -Wall -Wextra -Wpedantic -Wshadow)
endif()

if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Gy> $<$<CONFIG:Release>:/Gw>)
    add_link_options($<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
    add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    if(APPLE)
        add_link_options($<$<CONFIG:Release>:-Wl,-dead_strip> $<$<CONFIG:Release>:-Wl,-S>)
    else()
        add_link_options($<$<CONFIG:Release>:-Wl,--gc-sections> $<$<CONFIG:Release>:-Wl,--strip-all>)
    endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT whole LANGUAGES C CXX)
if(whole)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

set(asmjit_commit 0d7f01052f2d3f6d6b9bb6714ef32bd7b80177eb)
set(lua_version 5.5.1)

include(FetchContent)

function(vendor name file url)
    if(EXISTS "${vendors}/${name}/${file}")
        return()
    endif()
    message(STATUS "Downloading ${name} into ${vendors}/${name}")
    file(REMOVE_RECURSE "${binary}/_deps/${name}-subbuild")
    FetchContent_Populate(${name} QUIET
            URL "${url}"
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE
            SOURCE_DIR "${vendors}/${name}"
            SUBBUILD_DIR "${binary}/_deps/${name}-subbuild"
            BINARY_DIR "${binary}/_deps/${name}-build")
    if(NOT EXISTS "${vendors}/${name}/${file}")
        message(FATAL_ERROR "${url} unpacked into ${vendors}/${name} with no ${file} in it")
    endif()
endfunction()

vendor(asmjit CMakeLists.txt "https://github.com/asmjit/asmjit/archive/${asmjit_commit}.tar.gz")
vendor(lua src/lua.h "https://www.lua.org/ftp/lua-${lua_version}.tar.gz")

set(ASMJIT_STATIC ON)
set(ASMJIT_NO_FOREIGN OFF)
set(ASMJIT_NO_UJIT ON)
set(ASMJIT_NO_INSTALL ON)
add_subdirectory("${vendors}/asmjit" "${binary}/vendors/asmjit" EXCLUDE_FROM_ALL SYSTEM)

set(interpreter "${vendors}/lua/src")
file(GLOB library CONFIGURE_DEPENDS "${interpreter}/*.c")
list(REMOVE_ITEM library "${interpreter}/lua.c" "${interpreter}/luac.c")

add_library(lua STATIC ${library})
add_library(lua::lua ALIAS lua)
target_include_directories(lua SYSTEM PUBLIC "${interpreter}")

set_target_properties(lua PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
elseif(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
elseif(UNIX)
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX LUA_USE_DLOPEN)
elseif(MSVC)
    target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(UNIX)
    target_link_libraries(lua PRIVATE ${CMAKE_DL_LIBS} m)
endif()

find_package(Threads REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${source}/src/*.cpp"
        "${source}/src/*.c"
)

file(GLOB_RECURSE headers CONFIGURE_DEPENDS
        "${source}/include/*.hpp"
        "${source}/include/*.h"
        "${source}/src/*.hpp"
        "${source}/src/*.h"
)

add_executable(luajit ${sources} ${headers})
target_compile_options(luajit PRIVATE ${warnings})
target_include_directories(luajit PRIVATE "${source}/include" "${source}/src")
target_link_libraries(luajit PRIVATE asmjit::asmjit lua::lua Threads::Threads)

install(TARGETS luajit RUNTIME DESTINATION bin)