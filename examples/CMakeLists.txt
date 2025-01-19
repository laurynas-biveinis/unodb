# Copyright 2024-2025 Laurynas Biveinis
cmake_minimum_required(VERSION 3.16)

# This is a top-level CMake script and not an include of ../CMakeLists.txt. In
# fact, this one includes the one in the parent directory to show how another
# project can integrate UnoDB in its build.

project(unodb-examples VERSION 0.1
  DESCRIPTION "unodb key-value store library examples"
  HOMEPAGE_URL "https://github.com/laurynas-biveinis/unodb" LANGUAGES CXX)

add_subdirectory(.. unodb)

add_executable(example_art example_art.cpp)
target_link_libraries(example_art PRIVATE unodb)

add_executable(example_art_stats example_art_stats.cpp)
target_link_libraries(example_art_stats PRIVATE unodb)

add_executable(example_olc_art example_olc_art.cpp)
target_link_libraries(example_olc_art PRIVATE unodb)

add_custom_target(examples
  env ./example_art
  COMMAND env ./example_art_stats
  COMMAND env ./example_olc_art
  DEPENDS example_art example_art_stats example_olc_art)
