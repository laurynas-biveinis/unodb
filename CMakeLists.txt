cmake_minimum_required(VERSION 3.10)

project(unodb VERSION 0.1
  DESCRIPTION "unodb key-value store library"
  HOMEPAGE_URL "https://github.com/laurynas-biveinis/unodb" LANGUAGES CXX)

find_program(CLANG_TIDY_EXE NAMES "clang-tidy"
  DOC "Path to clang-tidy executable")
if(NOT CLANG_TIDY_EXE)
  message(STATUS "clang-tidy not found")
else()
  message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
  set(DO_CLANG_TIDY "${CLANG_TIDY_EXE}" "-p=${CMAKE_BINARY_DIR}" "-checks=*")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(GSL_INCLUDES "GSL/include")

add_library(unodb art.cpp art.hpp)
target_include_directories(unodb SYSTEM PUBLIC "${GSL_INCLUDES}")
target_compile_features(unodb PUBLIC cxx_std_17)
set_target_properties(unodb PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(unodb PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
if(DO_CLANG_TIDY)
  set_target_properties(unodb PROPERTIES CXX_CLANG_TIDY "${DO_CLANG_TIDY}")
endif()

add_subdirectory(googletest)

enable_testing()

add_executable(test_unodb test_unodb.cpp)
target_link_libraries(test_unodb PRIVATE unodb gtest_main)
if(DO_CLANG_TIDY)
  set_target_properties(test_unodb PROPERTIES CXX_CLANG_TIDY "${DO_CLANG_TIDY}")
endif()
add_test(test_unodb test_unodb)

