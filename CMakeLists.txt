cmake_minimum_required(VERSION 3.25)

project(luajit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Lua is C99 with the POSIX extensions its own Makefile asks for (gnu99).
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# A build that names no configuration is a release one. An IDE's profiles
# and the presets always name one; a bare `cmake -B build` does not.
get_property(multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT multi AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "The configuration to build" FORCE)
endif()

# The C and C++ runtimes linked into the program rather than loaded beside it,
# so the executable runs on a Windows machine with no redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(source "${CMAKE_CURRENT_SOURCE_DIR}")
set(binary "${CMAKE_CURRENT_BINARY_DIR}")
set(vendors "${source}/vendors")

# The program lands in bin/ beside the sources, whichever build tree made it.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${source}/bin")

# Warnings are errors waiting to be read. They are given to this project's
# own targets only: asmjit and Lua are compiled with their own settings, and
# their headers are marked as system ones, so none of their noise -- which
# this project cannot act on -- buries ours.
if(MSVC)
    set(warnings /W4 /permissive-)
else()
    set(warnings -Wall -Wextra -Wpedantic -Wshadow)
endif()

# A release build carries nothing it does not run: every function and
# constant nothing reaches is dropped at link time, identical ones are
# folded into one, symbols are stripped, and the whole program -- asmjit
# included -- is optimised as one rather than a file at a time. Lua is the
# exception, below.
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

# asmjit and Lua are built from source, from vendors/, as part of this build.
# A checkout that carries them configures with no network at all. One that
# does not downloads the pinned version into vendors/ on its first configure,
# and every configure after that finds it there and downloads nothing. To
# move to another version, change it here and delete that library's folder.
set(asmjit_commit 0d7f01052f2d3f6d6b9bb6714ef32bd7b80177eb)
set(lua_version 5.5.1)

include(FetchContent)

# vendor(<name> <file> <url>): unpacks the archive at <url> into
# vendors/<name> unless <file> is already there. The archive's own top-level
# folder (asmjit-<commit>/, lua-<version>/) is dropped, and its files are
# dated when they are unpacked, so a version changed here is recompiled
# rather than taken as older than the objects already built.
#
# The download's stamps from an earlier configure are cleared first: they
# would otherwise report a folder deleted since as still downloaded, and
# leave it empty. And <file> is checked for afterwards, so an archive laid
# out differently stops the configure here, by name, rather than further
# down as a library with no sources.
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

# asmjit as a static library holding the backend of the machine it is built
# for and nothing else: no foreign architectures, no UniCompiler, and no
# install rules of its own, since it is linked into the program, not shipped
# beside it. Plain variables, which asmjit's option() calls respect.
set(ASMJIT_STATIC ON)
set(ASMJIT_NO_FOREIGN ON)
set(ASMJIT_NO_UJIT ON)
set(ASMJIT_NO_INSTALL ON)
add_subdirectory("${vendors}/asmjit" "${binary}/vendors/asmjit" EXCLUDE_FROM_ALL SYSTEM)

# Lua has no CMake build of its own, so it is described here: the library is
# every file under src/ except the two programs, lua.c and luac.c, which
# carry a main() each. The platform is the one its Makefile would pick --
# LUA_USE_C89 is deliberately not used, since on Windows it would make
# lua_Integer a 32-bit long.
set(interpreter "${vendors}/lua/src")
file(GLOB library CONFIGURE_DEPENDS "${interpreter}/*.c")
list(REMOVE_ITEM library "${interpreter}/lua.c" "${interpreter}/luac.c")

add_library(lua STATIC ${library})
add_library(lua::lua ALIAS lua)
target_include_directories(lua SYSTEM PUBLIC "${interpreter}")

# Compiled whole when it is compiled, not again at link time: GCC, analysing
# lstrlib.c's gsub across the whole program, reports a memcpy of SIZE_MAX - 1
# bytes that no path reaches -- a warning in Lua's code that would surface in
# the link of every program using string.gsub, where nothing marks it as
# Lua's.
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

# require() of a C module goes through dlopen, and the math library goes
# through libm, on every Unix.
if(UNIX)
    target_link_libraries(lua PRIVATE ${CMAKE_DL_LIBS} m)
endif()

find_package(Threads REQUIRED)

# Every source under src/ but main.cpp is the engine, and the glob sees a
# file the next time the build runs, so a file added is a file compiled.
file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${source}/src/*.cpp"
        "${source}/src/*.c"
)

set(engine "${sources}")
list(REMOVE_ITEM engine "${source}/src/main.cpp")

file(GLOB_RECURSE headers CONFIGURE_DEPENDS
        "${source}/include/*.hpp"
        "${source}/include/*.h"
        "${source}/src/*.hpp"
        "${source}/src/*.h"
)

# The engine, compiled once, as the target core: the program and anything
# added later -- tests, a library -- link this rather than each compiling all
# of it again. Until src/ holds anything but main.cpp, core is an interface
# library: it carries the include paths, asmjit and Lua to whatever links it,
# and compiles nothing, so no placeholder source has to be written for it.
if(engine)
    add_library(core STATIC ${engine} ${headers})
    target_compile_options(core PRIVATE ${warnings})
    set(scope PUBLIC)
else()
    add_library(core INTERFACE ${headers})
    set(scope INTERFACE)
endif()

target_include_directories(core ${scope} "${source}/include" "${source}/src")
target_link_libraries(core ${scope} asmjit::asmjit lua::lua Threads::Threads)

add_executable(luajit "${source}/src/main.cpp")
target_link_libraries(luajit PRIVATE core)
target_compile_options(luajit PRIVATE ${warnings})
target_compile_definitions(luajit PRIVATE LUAJIT_VERSION="${PROJECT_VERSION}")

install(TARGETS luajit RUNTIME DESTINATION bin)