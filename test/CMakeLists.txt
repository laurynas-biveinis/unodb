# Copyright 2021 Laurynas Biveinis

enable_testing()

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

if(DO_CLANG_TIDY)
  # TODO(laurynas): introduce subdir, move this to own .clang-tidy
  string(CONCAT CLANG_TIDY_DISABLED_FOR_DEEPSTATE
    "-cert-err58-cpp,"
    "-cppcoreguidelines-avoid-non-const-global-variables," # TEST() macros
    "-fuchsia-statically-constructed-objects,"
    "-readability-implicit-bool-conversion") # DeepState_Bool returning int
  set(DO_CLANG_TIDY_DEEPSTATE ${DO_CLANG_TIDY}
    "-checks=${CLANG_TIDY_DISABLED_FOR_DEEPSTATE}")
endif()

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

add_library(db_test_utils STATIC db_test_utils.hpp db_test_utils.cpp)
common_target_properties(db_test_utils)
target_link_libraries(db_test_utils PRIVATE unodb gtest_main)
set_clang_tidy_options(db_test_utils "${DO_CLANG_TIDY}")

# FIXME(laurynas): unduplicate
add_library(qsbr_test_utils STATIC qsbr_test_utils.hpp qsbr_test_utils.cpp)
common_target_properties(qsbr_test_utils)
target_link_libraries(qsbr_test_utils PRIVATE unodb_qsbr gtest_main)
set_clang_tidy_options(qsbr_test_utils "${DO_CLANG_TIDY}")

# FIXME(laurynas): merge with add_db_test_target, pass which test utils libs
# to link with
function(ADD_TEST_TARGET TARGET)
  add_executable("${TARGET}" "${TARGET}.cpp")
  common_target_properties("${TARGET}")
  target_link_libraries("${TARGET}" PRIVATE unodb_qsbr gtest_main)
  set_clang_tidy_options("${TARGET}" "${DO_CLANG_TIDY}")
  add_test(NAME "${TARGET}" COMMAND "${TARGET}")
  if(SANITIZE_ADDRESS OR SANITIZE_THREAD OR SANITIZE_UB)
    set_property(TEST ${TARGET} APPEND PROPERTY ENVIRONMENT "${SANITIZER_ENV}")
  endif()
endfunction()

function(ADD_DB_TEST_TARGET TARGET)
  add_test_target("${TARGET}")
  target_link_libraries("${TARGET}" PRIVATE unodb db_test_utils)
endfunction()

add_test_target(test_qsbr)
target_link_libraries(test_qsbr PRIVATE qsbr_test_utils)
add_db_test_target(test_art)
add_db_test_target(test_art_concurrency)
target_link_libraries(test_art_concurrency PRIVATE qsbr_test_utils)

if(COVERAGE)
  add_custom_target(tests_for_coverage ctest -E test_art_fuzz_deepstate
    DEPENDS test_art test_art_concurrency test_qsbr)
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
    COMMAND env ${SANITIZER_ENV}
    ./test_art_fuzz_deepstate_lf deepstate_lf_corpus/ -use_value_profile=1
    -detect_leaks=0  -max_total_time=60)

  add_custom_target(deepstate_lf_20m
    ${CMAKE_COMMAND} -E make_directory deepstate_lf_corpus
    COMMAND env ${SANITIZER_ENV}
    ./test_art_fuzz_deepstate_lf deepstate_lf_corpus/ -use_value_profile=1
    -detect_leaks=0  -max_total_time=1200)

  add_custom_target(deepstate_lf_8h
    ${CMAKE_COMMAND} -E make_directory deepstate_lf_corpus
    COMMAND env ${SANITIZER_ENV}
    ./test_art_fuzz_deepstate_lf deepstate_lf_corpus/ -use_value_profile=1
    -detect_leaks=0  -max_total_time=28800)

  if(COVERAGE)
    add_coverage_target(TARGET coverage_deepstate_lf DEPENDENCY deepstate_lf_1m)
  endif()
endif()

add_custom_target(valgrind
  COMMAND valgrind --error-exitcode=1 --leak-check=full ./test_art;
  COMMAND valgrind --error-exitcode=1 --leak-check=full ./test_art_concurrency
  DEPENDS test_art test_art_concurrency valgrind_benchmarks)
