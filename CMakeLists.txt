# Copyright 2019-2020 Laurynas Biveinis
cmake_minimum_required(VERSION 3.12)

project(unodb VERSION 0.1
  DESCRIPTION "unodb key-value store library"
  HOMEPAGE_URL "https://github.com/laurynas-biveinis/unodb" LANGUAGES CXX)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"
    OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  set(CXX_WARNING_FLAGS
    "-Werror"
    # Warning groups
    "-Wall" "-Wextra" "-Wconversion" "-Wdelete-non-virtual-dtor" "-Wdeprecated"
    "-Wgnu" "-Wimplicit" "-Wloop-analysis" "-Wparentheses" "-Wpedantic"
    "-Wpragmas" "-Wself-assign" "-Wshadow-all"
    # Individual warnings
    "-Wabstract-vbase-init" "-Warray-bounds-pointer-arithmetic" "-Wassign-enum"
    "-Watomic-implicit-seq-cst" "-Wbad-function-cast" "-Wc++2a-compat"
    "-Wc++2a-extensions" "-Wcast-align" "-Wcast-qual" "-Wclass-varargs"
    "-Wcomma" "-Wconditional-uninitialized" "-Wcovered-switch-default"
    "-Wdate-time" "-Wdeprecated-implementations" "-Wdisabled-macro-expansion"
    "-Wdouble-promotion" "-Wduplicate-decl-specifier" "-Wduplicate-enum"
    "-Wduplicate-method-arg" "-Wduplicate-method-match" "-Wextra-semi-stmt"
    "-Wfloat-equal" "-Wformat-pedantic" "-Wformat=2" "-Wheader-hygiene"
    "-Widiomatic-parentheses" "-Wimplicit-fallthrough" "-Wmain"
    "-Wmethod-signatures" "-Wmissing-noreturn" "-Wmissing-prototypes"
    "-Wmissing-variable-declarations" "-Wnewline-eof" "-Wnon-virtual-dtor"
    "-Wnonportable-system-include-path" "-Wold-style-cast" "-Wover-aligned"
    "-Wpacked" "-Wpointer-arith" "-Wprofile-instr-missing" "-Wredundant-parens"
    "-Wreserved-id-macro" "-Wshift-sign-overflow" "-Wstatic-in-inline"
    "-Wstrict-prototypes" "-Wsuper-class-method-mismatch" "-Wswitch-enum"
    "-Wtautological-compare" "-Wtautological-constant-in-range-compare"
    "-Wundef" "-Wundefined-func-template" "-Wundefined-reinterpret-cast"
    "-Wunreachable-code-aggressive" "-Wunused-exception-parameter"
    "-Wunused-macros" "-Wunused-member-function" "-Wunused-template"
    "-Wused-but-marked-unused" "-Wvector-conversion" "-Wvla"
    "-Wweak-template-vtables" "-Wweak-vtables" "-Wzero-as-null-pointer-constant")
else()
  set(CXX_WARNING_FLAGS
    "-Werror"
    # Warning groups
    "-Wall" "-Wextra" "-Wpedantic" "-Wunused" "-Wparentheses" "-Wconversion"
    # Individual warnings
    "-Wabi-tag" "-Wcast-align=strict" "-Wcast-qual" "-Wcatch-value=3"
    "-Wctor-dtor-privacy" "-Wdouble-promotion" "-Wduplicated-branches"
    "-Wduplicated-cond" "-Wextra-semi" "-Wfloat-equal" "-Wformat-overflow=2"
    "-Wformat-signedness" "-Wformat-truncation=2" "-Wformat=2"
    "-Wimplicit-fallthrough=5" "-Winvalid-pch" "-Wlogical-op"
    "-Wmissing-declarations" "-Wmissing-include-dirs" "-Wnoexcept"
    "-Wnon-virtual-dtor" "-Wnull-dereference" "-Wold-style-cast"
    "-Woverloaded-virtual" "-Wpacked" "-Wplacement-new=2" "-Wredundant-decls"
    "-Wshadow=global" "-Wsign-conversion" "-Wsign-promo"
    "-Wstrict-null-sentinel" "-Wstringop-truncation" "-Wsuggest-attribute=cold"
    "-Wsuggest-attribute=const" "-Wsuggest-attribute=format"
    "-Wsuggest-attribute=malloc" "-Wsuggest-attribute=noreturn"
    "-Wsuggest-attribute=pure" "-Wsuggest-final-methods" "-Wsuggest-final-types"
    "-Wsuggest-override" "-Wswitch-enum" "-Wtrampolines" "-Wundef"
    "-Wuninitialized" "-Wunsafe-loop-optimizations" "-Wunused-const-variable=2"
    "-Wunused-macros" "-Wuseless-cast" "-Wvector-operation-performance"
    "-Wvla" "-Wzero-as-null-pointer-constant" "-Wattribute-alias=2"
    "-Warray-bounds=2" "-Wredundant-tags")
endif()

list(APPEND CXX_FLAGS "${CXX_WARNING_FLAGS}" "-g" "-msse4.1")

option(COVERAGE "Enable code coverage reporting")
if(COVERAGE)
  list(APPEND CXX_FLAGS "--coverage")
  list(APPEND INTERFACE_LD_FLAGS "--coverage")
  option(GCOV_PATH "gcov tool location to be used by lcov")
  if(GCOV_PATH)
    set(LCOV_GCOV_ARG "--gcov-tool" "${GCOV_PATH}")
    message(STATUS "Code coverage reporting enabled with gcov at ${GCOV_PATH}")
  else()
    set(LCOV_GCOV_ARG "")
    message(STATUS "Code coverage reporting enabled with default gcov path")
  endif()
else()
  message(STATUS "Code coverage reporting not enabled")
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
  find_path(VALGRIND_H_PATH valgrind.h PATH_SUFFIXES valgrind)
  if(VALGRIND_H_PATH)
    find_path(MEMCHECK_H_PATH memcheck.h PATH_SUFFIXES valgrind)
    if(MEMCHECK_H_PATH)
      message(STATUS "valgrind.h & memcheck.h found, enabling Valgrind client requests")
      add_compile_definitions(VALGRIND_CLIENT_REQUESTS)
    else()
      message(STATUS "valgrind.h found but memcheck.h not found, disabling Valgrind client requests")
    endif()
  else()
    message(STATUS "valgrind.h not found, disabling Valgrind client requests")
  endif()
  # TODO(laurynas): switch to -Og once it actually works in the oldest
  # supported compilers
  list(APPEND CXX_FLAGS "-O0")
else()
  if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
      "${CMAKE_CXX_COMPILER_VERSION}" VERSION_GREATER_EQUAL 9)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_SUPPORT_ERROR LANGUAGES CXX)
    if(IPO_SUPPORTED)
      message(STATUS "IPO/LTO supported")
    else()
      message(STATUS "IPO/LTO is not supported: ${IPO_SUPPORT_ERROR}")
    endif()
  endif()
  if(NOT COVERAGE)
    list(APPEND CXX_FLAGS "-O3")
  else()
    list(APPEND CXX_FLAGS "-O0")
  endif()
endif()

macro(ADD_TO_SANITIZER_FLAGS)
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    list(APPEND SANITIZER_CXX_FLAGS ${ARGV})
    list(APPEND SANITIZER_LD_FLAGS ${ARGV})
  endif()
endmacro()

macro(SET_UBSAN_ENV)
  string(CONCAT UBSAN_ENV "UBSAN_ENV="
    "print_stacktrace=1:halt_on_error=1:abort_on_error=1")
  string(CONCAT SANITIZER_ENV "${ASAN_ENV}" " " "${UBSAN_ENV}")
endmacro()

macro(SET_COMMON_SANITIZER_FLAGS)
  list(APPEND SANITIZER_CXX_FLAGS "-fsanitize=undefined"
    "-fno-omit-frame-pointer" "-fno-optimize-sibling-calls")
  # Boost library 1.72 on macOS from Homebrew produces the following error:
  # SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /Users/laurynas/unodb/unodb/heap.hpp:83:8 in 
  # /usr/local/include/boost/container/pmr/memory_resource.hpp:49:20: runtime error: member call on address 0x000105abd0b
  # 0 which does not point to an object of type 'boost::container::pmr::memory_resource'
  # 0x000105abd0b0: note: object is of type 'boost::container::pmr::new_delete_resource_imp'
  # It looks like it's because new_delete_resource_imp is not exported as a
  # visible symbol in the library?
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang"
      AND CMAKE_BUILD_TYPE MATCHES "Release")
    list(APPEND SANITIZER_CXX_FLAGS "-fno-sanitize=vptr")
  endif()
  set(SANITIZER_LD_FLAGS "-fsanitize=undefined")
  set_ubsan_env()
endmacro()

option(SANITIZE
  "Enable AddressSanitizer and UndefinedBehaviorSanitizer runtime checks")
if(SANITIZE)
  set_common_sanitizer_flags()
  list(APPEND SANITIZER_CXX_FLAGS "-fsanitize=address")
  list(APPEND SANITIZER_LD_FLAGS "-fsanitize=address")
  add_to_sanitizer_flags("-fsanitize=leak" "-fsanitize-address-use-after-scope"
    "-fsanitize=pointer-compare" "-fsanitize=pointer-subtract")
  string(CONCAT ASAN_ENV "ASAN_ENV="
    "check_initialization_order=true:detect_stack_use_after_return=true:"
    "alloc_dealloc_mismatch=true:strict_string_checks=true")
  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND
      "${CMAKE_CXX_COMPILER_VERSION}" VERSION_GREATER_EQUAL 8)
    # ERROR: AddressSanitizer: invalid-pointer-pair: 0x7fffe8404697
    # 0x000000000000
    # #0 0x55d21abf7a40 in std::__cxx11::basic_stringbuf<char,
    # std::char_traits<char>, std::allocator<char> >::str() const
    # /usr/include/c++/8/sstream:173
    string(APPEND ASAN_ENV ":detect_invalid_pointer_pairs=1")
  else()
    string(APPEND ASAN_ENV ":detect_invalid_pointer_pairs=2")
  endif()
  if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    string(APPEND ASAN_ENV ":detect_leaks=1")
  endif()
  set_ubsan_env()
endif()

option(SANITIZE_THREAD
  "Enable ThreadSanitizer and UndefinedBehaviorSanitizer runtime checks")
if(SANITIZE_THREAD)
  set_common_sanitizer_flags()
  list(APPEND SANITIZER_CXX_FLAGS "-fsanitize=thread")
  list(APPEND SANITIZER_LD_FLAGS "-fsanitize=thread")
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
  set(DEBUG "ON")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)

configure_file(config.hpp.in config.hpp)

find_package(Threads REQUIRED)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  message(STATUS "Using GCC, using std::pmr instead of boost::pmr")
  set(USE_BOOST FALSE)
else()
  # Workaround https://github.com/boostorg/boost_install/issues/13: Homebrew
  # Boost 1.71 installs single-thread and MT build to the same prefix, resulting
  # in -
  # CMake Warning at /usr/local/lib/cmake/boost_container-1.71.0/libboost_container-variant-shared.cmake:59 (message):
  # Target Boost::container already has an imported location
  # '/usr/local/lib/libboost_container-mt.dylib', which will be overwritten
  # with '/usr/local/lib/libboost_container.dylib'
  set(Boost_NO_BOOST_CMAKE ON)
  find_package(Boost REQUIRED COMPONENTS container)
  set(USE_BOOST TRUE)
endif()

string(REPLACE ";" " " CXX_FLAGS_FOR_SUBDIR_STR "${SANITIZER_CXX_FLAGS}")
string(APPEND CMAKE_CXX_FLAGS ${CXX_FLAGS_FOR_SUBDIR_STR})
string(REPLACE ";" " " LD_FLAGS_FOR_SUBDIR_STR "${SANITIZER_LD_FLAGS}")
string(APPEND CMAKE_EXE_LINKER_FLAGS "${LD_FLAGS_FOR_SUBDIR_STR}")
string(APPEND CMAKE_MODULE_LINKER_FLAGS "${LD_FLAGS_FOR_SUBDIR_STR}")
string(APPEND CMAKE_SHARED_LINKER_FLAGS "${LD_FLAGS_FOR_SUBDIR_STR}")
add_subdirectory(3rd_party/googletest)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Suppressing Google Benchmark tests"
  FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL
  "Suppressing Google Benchmark installation" FORCE)
if(IPO_SUPPORTED AND NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  set(BENCHMARK_ENABLE_LTO ON CACHE BOOL "Enabling LTO for Google Benchmark"
    FORCE)
  message(STATUS "Enabling LTO for Google Benchmark")
else()
  message(STATUS "Disabling LTO for Google Benchmark")
endif()
add_subdirectory(3rd_party/benchmark)
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU"
    AND CMAKE_BUILD_TYPE MATCHES "Debug")
  target_compile_definitions(benchmark PUBLIC _GLIBCXX_DEBUG
    _GLIBCXX_DEBUG_PEDANTIC)
endif()
# Add benchmark_include_dirs by target_include_directories(... SYSTEM ...)
# before target_link_libraries so that benchmark headers are included through
# -isystem not -I, resulting in build-breaking diagnostics.
get_target_property(benchmark_include_dirs benchmark::benchmark
  INTERFACE_INCLUDE_DIRECTORIES)

# TODO(laurynas): convert DeepState dependency to a git submodule
find_path(DEEPSTATE_HPP_PATH deepstate/DeepState.hpp)
if(NOT DEEPSTATE_HPP_PATH)
  message(STATUS "DeepState header not found, not building its fuzz tests")
else()
  message(STATUS "DeepState header found in ${DEEPSTATE_HPP_PATH}")
endif()

find_library(DEEPSTATE_LIB_PATH deepstate)
if(NOT DEEPSTATE_LIB_PATH)
  message(STATUS "DeepState library not found, not building its fuzz tests")
else()
  message(STATUS "DeepState library found in ${DEEPSTATE_LIB_PATH}")
endif()

include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
check_cxx_source_compiles(
  "#include <cstddef>
   #include <cstdint>
   extern \"C\" int LLVMFuzzerTestOneInput(const std::uint8_t *, std::size_t) {
     return 0;
   }" LIBFUZZER_OK)

if(LIBFUZZER_OK)
  find_library(DEEPSTATE_LF_LIB_PATH deepstate_LF)
  if(NOT DEEPSTATE_LF_LIB_PATH)
    message(STATUS "libfuzzer-enabled DeepState not found, not building its fuzz tests")
  else()
    message(STATUS "libfuzzer-enabled DeepState found in ${DEEPSTATE_LF_LIB_PATH}")
  endif()
endif()

if(DEEPSTATE_HPP_PATH)
  if(DEEPSTATE_LIB_PATH)
    set(DEEPSTATE_OK TRUE)
  endif()
  if(DEEPSTATE_LF_LIB_PATH)
    set(DEEPSTATE_LF_OK TRUE)
  endif()
endif()

if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"
    AND NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  message(STATUS "Not using clang-tidy due to non-clang compiler being used")
else()
  find_program(CLANG_TIDY_EXE NAMES "clang-tidy"
    DOC "Path to clang-tidy executable")
  if(NOT CLANG_TIDY_EXE)
    message(STATUS "clang-tidy not found")
  else()
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
    string(CONCAT CLANG_TIDY_DISABLED
      "-fuchsia-default-arguments,"
      "-clang-diagnostic-error,"
      # Duplicated by modernize-avoid-c-arrays
      "-cppcoreguidelines-avoid-c-arrays,"
      "-cppcoreguidelines-avoid-magic-numbers,"
      "-cppcoreguidelines-init-variables,"
      "-cppcoreguidelines-macro-usage," # Until it respects __LINE__ in macro definition
      "-cppcoreguidelines-non-private-member-variables-in-classes,"
      "-cppcoreguidelines-pro-bounds-array-to-pointer-decay,"
      "-cppcoreguidelines-pro-bounds-constant-array-index,"
      # Because leaf nodes are std::byte arrays
      "-cppcoreguidelines-pro-bounds-pointer-arithmetic,"
      # Because VALGRIND_MALLOCLIKE_BLOCK expands to C-style cast, and we have
      # -Wold-style-cast anyway
      "-cppcoreguidelines-pro-type-const-cast,"
      "-cppcoreguidelines-pro-type-cstyle-cast,"
      "-cppcoreguidelines-pro-type-member-init,"
      "-cppcoreguidelines-pro-type-reinterpret-cast,"
      "-cppcoreguidelines-pro-type-static-cast-downcast,"
      "-cppcoreguidelines-pro-type-union-access,"
      "-fuchsia-default-arguments-calls,"
      "-fuchsia-overloaded-operator,"
      "-google-readability-braces-around-statements,"
      "-google-runtime-references,"
      # Duplicated by modernize-avoid-c-arrays
      "-hicpp-avoid-c-arrays,"
      "-hicpp-braces-around-statements,"
      "-hicpp-member-init,"
      "-hicpp-no-array-decay,"
      "-hicpp-no-assembler," # Valgrind client requests
      "-hicpp-use-equals-default,"
      "-llvm-include-order,"
      "-misc-non-private-member-variables-in-classes,"
      "-modernize-use-equals-default," # Until foo() noexcept = default is accepted by clang
      "-modernize-use-trailing-return-type,"
      "-portability-simd-intrinsics,"
      "-readability-braces-around-statements,"
      "-readability-named-parameter,"
      "-readability-magic-numbers")
    set(DO_CLANG_TIDY_COMMON "${CLANG_TIDY_EXE}" "-p=${CMAKE_BINARY_DIR}"
      "-warnings-as-errors=*")
    set(DO_CLANG_TIDY ${DO_CLANG_TIDY_COMMON} "-checks=*,${CLANG_TIDY_DISABLED}")
    string(CONCAT CLANG_TIDY_DISABLED_FOR_TESTS
      "${CLANG_TIDY_DISABLED},"
      "-build-include-order,"
      "-cert-err58-cpp,"
      "-cppcoreguidelines-avoid-goto,"
      "-cppcoreguidelines-owning-memory,"
      "-cppcoreguidelines-pro-type-vararg," # GTest uses varargs
      "-cppcoreguidelines-special-member-functions,"
      "-fuchsia-statically-constructed-objects,"
      "-google-runtime-references,"
      "-hicpp-avoid-goto,"
      "-hicpp-special-member-functions,"
      "-hicpp-vararg") # GTest uses varargs
    set(DO_CLANG_TIDY_TEST ${DO_CLANG_TIDY_COMMON}
      "-checks=*,${CLANG_TIDY_DISABLED_FOR_TESTS}")
    string(CONCAT CLANG_TIDY_DISABLED_FOR_DEEPSTATE
      "${CLANG_TIDY_DISABLED},"
      "-cert-err58-cpp,"
      "-fuchsia-statically-constructed-objects,"
      "-readability-implicit-bool-conversion") # DeepState_Bool returning int
    set(DO_CLANG_TIDY_DEEPSTATE ${DO_CLANG_TIDY_COMMON}
      "-checks=*,${CLANG_TIDY_DISABLED_FOR_DEEPSTATE}")
  endif()
endif()

option(CPPCHECK_AGGRESSIVE "Enable inconclusive and false positive cppcheck checks")
if(CPPCHECK_AGGRESSIVE)
  set(CPPCHECK_CHECKS "--inconclusive")
else()
  set(CPPCHECK_CHECKS
    # False positive on Google Test TEST macro and compilers are much better
    # for syntax checking anyway
    "--suppress=syntaxError"
    # False positive on pointer unions being passed by value
    "--suppress=passedByValue")
endif()

find_program(CPPCHECK_EXE NAMES "cppcheck" DOC "Path to cppcheck executable")
if(NOT CPPCHECK_EXE)
  message(STATUS "cppcheck not found")
else()
  execute_process(COMMAND "${CPPCHECK_EXE}" "--version" OUTPUT_VARIABLE
    CPPCHECK_VERSION_OUTPUT)
  message(STATUS
    "cppcheck found: ${CPPCHECK_EXE}, --version: ${CPPCHECK_VERSION_OUTPUT}")
  set(DO_CPPCHECK "${CPPCHECK_EXE}" "--enable=warning,style,performance,portability"
    "--error-exitcode=2" "-D__x86_64" "--inline-suppr")
  list(APPEND DO_CPPCHECK "${CPPCHECK_CHECKS}")
endif()

find_program(CPPLINT_EXE NAMES "cpplint" DOC "Path to cpplint executable")
if(NOT CPPLINT_EXE)
  message(STATUS "cpplint not found")
else()
  message(STATUS "cpplint found: ${CPPLINT_EXE}")
  string(CONCAT CPPLINT_DISABLED_TESTS "--filter="
    "-build/c++11," # <thread> is an unapproved C++11 header
    "-build/include_order,"
    "-readability/nolint," # clang-tidy owns NOLINT
    "-runtime/references,"
    # Does not understand C++17 structured bindings and we use clang-format
    # anyway
    "-whitespace/braces")
  set(DO_CPPLINT "${CPPLINT_EXE}" "${CPPLINT_DISABLED_TESTS}")
endif()

option(IWYU "Enable include-what-you-use checking")
if(IWYU)
  if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"
      AND NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    message(STATUS "Not using include-what-you-use due to non-clang compiler being used")
  else()
    find_program(IWYU_EXE NAMES "include-what-you-use"
      DOC "Path to include-what-you-use executable")
    if(NOT IWYU_EXE)
      message(STATUS "include-what-you-use not found")
    else()
      message(STATUS "include-what-you-use found: ${IWYU_EXE}")
      set(DO_IWYU "${IWYU_EXE}")
    endif()
  endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(GSL_INCLUDES "3rd_party/GSL/include")

function(COMMON_TARGET_PROPERTIES TARGET)
  target_include_directories(${TARGET} PRIVATE ${CMAKE_BINARY_DIR})
  target_compile_options(${TARGET} PRIVATE "${CXX_FLAGS}")
  target_compile_options(${TARGET} PRIVATE "${SANITIZER_CXX_FLAGS}")
  target_compile_features(${TARGET} PUBLIC cxx_std_17)
  set_target_properties(${TARGET} PROPERTIES CXX_EXTENSIONS OFF)
  # Change to target_link_options on 3.13 minimum CMake version
  target_link_libraries(${TARGET} PRIVATE "${LD_FLAGS}")
  target_link_libraries(${TARGET} PRIVATE "${SANITIZER_LD_FLAGS}")
  target_link_libraries(${TARGET} INTERFACE "${INTERFACE_LD_FLAGS}")
  if(IPO_SUPPORTED)
    set_target_properties(${TARGET} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
  if(CPPCHECK_EXE)
    set_target_properties(${TARGET} PROPERTIES CXX_CPPCHECK "${DO_CPPCHECK}")
  endif()
  if(CPPLINT_EXE)
    set_target_properties(${TARGET} PROPERTIES CXX_CPPLINT "${DO_CPPLINT}")
  endif()
  if(IWYU_EXE)
    set_target_properties(${TARGET} PROPERTIES CXX_INCLUDE_WHAT_YOU_USE "${DO_IWYU}")
  endif()
endfunction()

function(SET_CLANG_TIDY_OPTIONS TARGET COMMAND)
  if(DO_CLANG_TIDY)
    set_target_properties(${TARGET} PROPERTIES CXX_CLANG_TIDY "${COMMAND}")
  endif()
endfunction()

add_library(unodb art.cpp art.hpp art_common.hpp global.hpp mutex_art.hpp
  heap.hpp)
common_target_properties(unodb)
target_include_directories(unodb PUBLIC ".")
target_include_directories(unodb SYSTEM PUBLIC "${GSL_INCLUDES}")
if(USE_BOOST)
  target_include_directories(unodb SYSTEM PRIVATE "${Boost_INCLUDE_DIRS}")
  target_link_libraries(unodb PRIVATE "${Boost_LIBRARIES}")
endif()
set_clang_tidy_options(unodb "${DO_CLANG_TIDY}")
target_link_libraries(unodb PUBLIC Threads::Threads)

enable_testing()

function(ADD_COVERAGE_TARGET)
  set(fn_options "")
  set(fn_one_value_args TARGET DEPENDENCY)
  set(fn_multi_value_args "")
  cmake_parse_arguments(ARG "${fn_options}" "${fn_one_value_args}"
    "${fn_multi_value_args}" ${ARGN})
  set(COV_DATA "coverage-${ARG_TARGET}.info")
  add_custom_target(${ARG_TARGET}
    COMMAND lcov ${LCOV_GCOV_ARG} --capture --directory . --output-file
    ${COV_DATA}
    COMMAND lcov ${LCOV_GCOV_ARG} --remove ${COV_DATA} '/usr/*' --output-file
    ${COV_DATA}
    COMMAND lcov ${LCOV_GCOV_ARG} --remove ${COV_DATA} '/Library/Developer/*'
    --output-file ${COV_DATA}
    COMMAND lcov ${LCOV_GCOV_ARG} --remove ${COV_DATA}
    '/Applications/Xcode.app/*' --output-file ${COV_DATA}
    COMMAND lcov ${LCOV_GCOV_ARG} --remove ${COV_DATA}
    '*/unodb/3rd_party/googletest/*' --output-file ${COV_DATA}
    COMMAND lcov ${LCOV_GCOV_ARG} --remove ${COV_DATA} '*/unodb/3rd_party/GSL/*'
    --output-file ${COV_DATA}
    COMMAND lcov ${LCOV_GCOV_ARG} --list ${COV_DATA}
    DEPENDS ${ARG_DEPENDENCY})
endfunction()

add_library(test_utils STATIC test_utils.hpp test_utils.cpp)
common_target_properties(test_utils)
target_link_libraries(test_utils PRIVATE unodb gtest_main)
set_clang_tidy_options(test_utils "${DO_CLANG_TIDY_TEST}")

function(ADD_TEST_TARGET TARGET)
  add_executable("${TARGET}" "${TARGET}.cpp")
  common_target_properties("${TARGET}")
  target_link_libraries("${TARGET}" PRIVATE unodb gtest_main test_utils)
  set_clang_tidy_options("${TARGET}" "${DO_CLANG_TIDY_TEST}")
  add_test(NAME "${TARGET}" COMMAND "${TARGET}")
  if(SANITIZE)
    set_tests_properties(${TARGET} PROPERTIES ENVIRONMENT "${ASAN_ENV}")
  endif()
  if(SANITIZE OR SANITIZE_THREAD)
    set_property(TEST ${TARGET} APPEND PROPERTY ENVIRONMENT "${UBSAN_ENV}")
  endif()
endfunction()

add_test_target(test_art)
add_test_target(test_art_mutex_concurrency)

if(COVERAGE)
  add_custom_target(tests_for_coverage ctest -E test_art_fuzz_deepstate
    DEPENDS test_art test_art_mutex_concurrency)
  add_coverage_target(TARGET coverage DEPENDENCY tests_for_coverage)
endif()

function(COMMON_DEEPSTATE_TARGET_PROPERTIES TARGET)
  common_target_properties(${TARGET})
  target_include_directories(${TARGET} SYSTEM PRIVATE "${DEEPSTATE_HPP_PATH}")
  target_link_libraries(${TARGET} PRIVATE unodb)
  set_clang_tidy_options(${TARGET} "${DO_CLANG_TIDY_DEEPSTATE}")
endfunction()

if(DEEPSTATE_OK)
  add_executable(test_art_fuzz_deepstate test_art_fuzz_deepstate.cpp)
  add_test(NAME test_art_fuzz_deepstate COMMAND test_art_fuzz_deepstate)
  common_deepstate_target_properties(test_art_fuzz_deepstate)
  target_link_libraries(test_art_fuzz_deepstate PRIVATE "${DEEPSTATE_LIB_PATH}")

  add_custom_target(deepstate_1m
    ${CMAKE_COMMAND} -E make_directory deepstate_fails
    COMMAND test_art_fuzz_deepstate --fuzz --timeout 60 --output_test_dir deepstate_fails)

  add_custom_target(deepstate_20m
    ${CMAKE_COMMAND} -E make_directory deepstate_fails
    COMMAND test_art_fuzz_deepstate --fuzz --timeout 1200 --output_test_dir deepstate_fails)

  add_custom_target(deepstate_8h
    ${CMAKE_COMMAND} -E make_directory deepstate_fails
    COMMAND test_art_fuzz_deepstate --fuzz --timeout 28800 --output_test_dir deepstate_fails)

  if(COVERAGE)
    add_coverage_target(TARGET coverage_deepstate DEPENDENCY deepstate_1m)
  endif()
endif()

if(DEEPSTATE_LF_OK)
  add_executable(test_art_fuzz_deepstate_lf test_art_fuzz_deepstate.cpp)
  add_test(NAME test_art_fuzz_deepstate_lf COMMAND test_art_fuzz_deepstate_lf -runs=1)
  common_deepstate_target_properties(test_art_fuzz_deepstate_lf)
  set_target_properties(test_art_fuzz_deepstate_lf PROPERTIES COMPILE_DEFINITIONS "LIBFUZZER")
  target_compile_options(test_art_fuzz_deepstate_lf PRIVATE "-fsanitize=fuzzer")
  target_link_libraries(test_art_fuzz_deepstate_lf PRIVATE "${DEEPSTATE_LF_LIB_PATH}")
  target_link_libraries(test_art_fuzz_deepstate_lf PRIVATE "-fsanitize=fuzzer")

  add_custom_target(deepstate_lf_1m
    ${CMAKE_COMMAND} -E make_directory deepstate_lf_corpus
    COMMAND env ${ASAN_ENV} ${UBSAN_ENV}
    ./test_art_fuzz_deepstate_lf deepstate_lf_corpus/ -use_value_profile=1
    -detect_leaks=0  -max_total_time=60)

  add_custom_target(deepstate_lf_20m
    ${CMAKE_COMMAND} -E make_directory deepstate_lf_corpus
    COMMAND env ${ASAN_ENV} ${UBSAN_ENV}
    ./test_art_fuzz_deepstate_lf deepstate_lf_corpus/ -use_value_profile=1
    -detect_leaks=0  -max_total_time=1200)

  add_custom_target(deepstate_lf_8h
    ${CMAKE_COMMAND} -E make_directory deepstate_lf_corpus
    COMMAND env ${ASAN_ENV} ${UBSAN_ENV}
    ./test_art_fuzz_deepstate_lf deepstate_lf_corpus/ -use_value_profile=1
    -detect_leaks=0  -max_total_time=28800)

  if(COVERAGE)
    add_coverage_target(TARGET coverage_deepstate_lf DEPENDENCY deepstate_lf_1m)
  endif()
endif()

add_subdirectory(benchmark)

add_custom_target(valgrind
  COMMAND valgrind --error-exitcode=1 --leak-check=full ./test_art;
  COMMAND valgrind --error-exitcode=1 --leak-check=full
  ./test_art_mutex_concurrency
  DEPENDS test_art test_art_mutex_concurrency valgrind_benchmarks)
