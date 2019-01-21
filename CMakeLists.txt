cmake_minimum_required(VERSION 3.10)

project(unodb VERSION 0.1
  DESCRIPTION "unodb key-value store library"
  HOMEPAGE_URL "https://github.com/laurynas-biveinis/unodb" LANGUAGES CXX)

add_library(unodb art.cpp art.hpp)
target_compile_features(unodb PUBLIC cxx_std_17)
set_target_properties(unodb PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(unodb PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)

add_subdirectory(googletest)

enable_testing()

add_executable(test_unodb test_unodb.cpp)
target_link_libraries(test_unodb PRIVATE unodb gtest_main)
add_test(test_unodb test_unodb)

