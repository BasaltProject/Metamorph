cmake_minimum_required(VERSION 3.28)

project(basalt-metamorph
    VERSION 26.3.0
    DESCRIPTION "Metamorph: converts and upgrades Minecraft worlds"
    LANGUAGES C CXX
)

# Standalone it brings its own Wire; inside Basalt Server the consumer has already added one.
if(NOT TARGET basalt::wire-nbt)
    option(BASALT_BUILD_TESTS "Build the test executables" ON)
    option(BASALT_WERROR "Treat warnings as errors" OFF)
    set(BASALT_SANITIZE "" CACHE STRING "Comma-separated sanitizers: address, undefined, thread")

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
    endif()

    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)

    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wire/CMakeLists.txt)
        message(FATAL_ERROR "wire/ is empty. Run: git submodule update --init")
    endif()
    if(BASALT_BUILD_TESTS)
        enable_testing()
    endif()
    add_subdirectory(wire)
    set(BASALT_METAMORPH_STANDALONE ON)
endif()

add_subdirectory(metamorph-region)
add_subdirectory(metamorph-datafix)

if(BASALT_METAMORPH_STANDALONE)
    add_subdirectory(tools/cli)
endif()
