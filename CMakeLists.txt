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
  string(CONCAT CLANG_TIDY_DISABLED_FOR_TESTS
    "-cert-err58-cpp,"
    "-cppcoreguidelines-owning-memory,"
    "-cppcoreguidelines-special-member-functions,"
    "-fuchsia-statically-constructed-objects,"
    "-hicpp-special-member-functions")
  set(DO_CLANG_TIDY_TEST "${CLANG_TIDY_EXE}" "-p=${CMAKE_BINARY_DIR}"
    "-checks=*,${CLANG_TIDY_DISABLED_FOR_TESTS}")
endif()

option(CPPCHECK_AGGRESSIVE "Enable inconclusive cppcheck checks")
if(CPPCHECK_AGGRESSIVE)
  set(CPPCHECK_AGGRESSIVE_OPT "--inconclusive")
else()
  set(CPPCHECK_AGGRESSIVE_OPT "")
endif()

find_program(CPPCHECK_EXE NAMES "cppcheck" DOC "Path to cppcheck executable")
if(NOT CPPCHECK_EXE)
  message(STATUS "cppcheck not found")
else()
  message(STATUS "cppcheck found: ${CPPCHECK_EXE}")
  set(DO_CPPCHECK "${CPPCHECK_EXE}" "--enable=warning,style,performance,portability"
    "${CPPCHECK_AGGRESSIVE_OPT}")
endif()

find_program(CPPLINT_EXE NAMES "cpplint" DOC "Path to cpplint executable")
if(NOT CPPLINT_EXE)
  message(STATUS "cpplint not found")
else()
  message(STATUS "cpplint found: ${CPPLINT_EXE}")
  set(DO_CPPLINT "${CPPLINT_EXE}")
endif()

option(IWYU "Enable include-what-you-use checking")
if(IWYU)
  find_program(IWYU_EXE NAMES "include-what-you-use"
    DOC "Path to include-what-you-use executable")
  if(NOT IWYU_EXE)
    message(STATUS "include-what-you-use not found")
  else()
    message(STATUS "include-what-you-use found: ${IWYU_EXE}")
    set(DO_IWYU "${IWYU_EXE}")
  endif()
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
if(CPPCHECK_EXE)
  set_target_properties(unodb PROPERTIES CXX_CPPCHECK "${DO_CPPCHECK}")
endif()
if(CPPLINT_EXE)
  set_target_properties(unodb PROPERTIES CXX_CPPLINT "${DO_CPPLINT}")
endif()
if(IWYU_EXE)
  set_target_properties(unodb PROPERTIES CXX_INCLUDE_WHAT_YOU_USE "${DO_IWYU}")
endif()

add_subdirectory(googletest)

enable_testing()

add_executable(test_unodb test_unodb.cpp)
target_link_libraries(test_unodb PRIVATE unodb gtest_main)
if(DO_CLANG_TIDY)
  set_target_properties(test_unodb PROPERTIES CXX_CLANG_TIDY "${DO_CLANG_TIDY_TEST}")
endif()
if(CPPCHECK_EXE)
  set_target_properties(test_unodb PROPERTIES CXX_CPPCHECK "${DO_CPPCHECK}")
endif()
if(CPPLINT_EXE)
  set_target_properties(test_unodb PROPERTIES CXX_CPPLINT "${DO_CPPLINT}")
endif()
if(IWYU_EXE)
  set_target_properties(test_unodb PROPERTIES CXX_INCLUDE_WHAT_YOU_USE "${DO_IWYU}")
endif()
add_test(test_unodb test_unodb)

