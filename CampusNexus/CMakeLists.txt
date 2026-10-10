cmake_minimum_required(VERSION 3.16)
project(CampusNexus VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core graph library: loading, autocomplete, positions, edit distance,
# Dijkstra, Bellman-Ford, cycle detection, topological sort + HTML/SVG map views.
add_library(campus_core STATIC src/campus_map.cpp src/visualizer.cpp)
target_include_directories(campus_core PUBLIC include)

add_executable(campus_nexus src/main.cpp)
target_link_libraries(campus_nexus PRIVATE campus_core)

add_executable(campus_tests tests/test_campus.cpp)
target_link_libraries(campus_tests PRIVATE campus_core)

set_target_properties(campus_nexus campus_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# Make the executables self-contained (no libstdc++-6.dll etc. needed at run time).
if(MINGW)
    target_link_options(campus_nexus PRIVATE -static)
    target_link_options(campus_tests PRIVATE -static)
elseif(MSVC)
    set_property(TARGET campus_core campus_nexus campus_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

enable_testing()
add_test(NAME campus_tests COMMAND campus_tests WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
