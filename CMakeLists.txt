cmake_minimum_required(VERSION 3.10)

project(unodb VERSION 0.1
  DESCRIPTION "unodb key-value store library"
  HOMEPAGE_URL "https://github.com/laurynas-biveinis/unodb" LANGUAGES CXX)

set(CXX_WARNING_FLAGS
  # Warning groups
  "-Wall" "-Wextra" "-Wconversion" "-Wdeprecated" "-Wgnu" "-Wimplicit"
  "-Wloop-analysis" "-Wpedantic" "-Wpragmas"  "-Wself-assign" "-Wshadow-all"
  "-Wthread-safety"
  # Individual warnings
  "-Wabstract-vbase-init" "-Warray-bounds-pointer-arithmetic" "-Wassign-enum"
  "-Wbad-function-cast" "-Wc++2a-compat" "-Wc++2a-extensions" "-Wcast-align"
  "-Wcast-qual" "-Wclass-varargs" "-Wcomma" "-Wconditional-uninitialized"
  "-Wcovered-switch-default" "-Wdate-time" "-Wdeprecated-implementations"
  "-Wdisabled-macro-expansion" "-Wdouble-promotion" "-Wduplicate-decl-specifier"
  "-Wduplicate-enum" "-Wduplicate-method-arg" "-Wduplicate-method-match"
  "-Wfloat-equal" "-Wformat-pedantic" "-Wformat=2" "-Wheader-hygiene"
  "-Widiomatic-parentheses" "-Wimplicit-fallthrough" "-Wmain"
  "-Wmethod-signatures" "-Wmissing-noreturn" "-Wmissing-prototypes"
  "-Wmissing-variable-declarations" "-Wnewline-eof" "-Wnon-virtual-dtor"
  "-Wnonportable-system-include-path" "-Wold-style-cast" "-Wover-aligned"
  "-Wpacked" "-Wpadded" "-Wpointer-arith" "-Wprofile-instr-missing"
  "-Wredundant-parens" "-Wreserved-id-macro" "-Wshift-sign-overflow"
  "-Wstatic-in-inline" "-Wstrict-prototypes" "-Wsuper-class-method-mismatch"
  "-Wswitch-enum" "-Wtautological-compare"
  "-Wtautological-constant-in-range-compare" "-Wthread-safety-beta"
  "-Wthread-safety-negative" "-Wthread-safety-verbose" "-Wundef"
  "-Wundefined-func-template" "-Wundefined-reinterpret-cast"
  "-Wunreachable-code-aggressive" "-Wunused-exception-parameter"
  "-Wunused-macros" "-Wunused-member-function" "-Wunused-template"
  "-Wused-but-marked-unused" "-Wvector-conversion" "-Wvla"
  "-Wweak-template-vtables" "-Wweak-vtables" "-Wzero-as-null-pointer-constant"
  )

if(CMAKE_BUILD_TYPE MATCHES Debug)
  SET(DEBUG "ON")
endif()

configure_file(config.hpp.in config.hpp)

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
  set(DO_CPPLINT "${CPPLINT_EXE}" "--filter=-runtime/references")
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
target_include_directories(unodb PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(unodb SYSTEM PUBLIC "${GSL_INCLUDES}")
target_compile_features(unodb PUBLIC cxx_std_17)
target_compile_options(unodb PRIVATE "${CXX_WARNING_FLAGS}")
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
target_include_directories(test_unodb PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(test_unodb PRIVATE "${CXX_WARNING_FLAGS}")
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
