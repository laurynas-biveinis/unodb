# Copyright 2021 Laurynas Biveinis

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

add_library(db_test_utils STATIC db_test_utils.hpp db_test_utils.cpp)
common_target_properties(db_test_utils)
target_link_libraries(db_test_utils PRIVATE unodb gtest_main)
set_clang_tidy_options(db_test_utils "${DO_CLANG_TIDY}")

add_library(qsbr_test_utils STATIC qsbr_test_utils.hpp qsbr_test_utils.cpp)
common_target_properties(qsbr_test_utils)
target_link_libraries(qsbr_test_utils PRIVATE unodb_qsbr gtest_main)
set_clang_tidy_options(qsbr_test_utils "${DO_CLANG_TIDY}")

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

add_test_target(test_qsbr_ptr)
add_test_target(test_qsbr)
target_link_libraries(test_qsbr PRIVATE qsbr_test_utils)
add_db_test_target(test_art)
add_db_test_target(test_art_concurrency)
target_link_libraries(test_art_concurrency PRIVATE qsbr_test_utils)

if(COVERAGE)
  add_custom_target(tests_for_coverage ctest -E
    DEPENDS test_art test_art_concurrency test_qsbr_ptr test_qsbr)
  add_coverage_target(TARGET coverage DEPENDENCY tests_for_coverage)
endif()

add_custom_target(valgrind_tests
  COMMAND ${VALGRIND_COMMAND} ./test_qsbr_ptr;
  # The death tests will print their SIGABRT stacktrace under Valgrind. I have
  # not found a way to disable it.
  COMMAND ${VALGRIND_COMMAND} ./test_qsbr;
  COMMAND ${VALGRIND_COMMAND} ./test_art;
  COMMAND ${VALGRIND_COMMAND} ./test_art_concurrency
  DEPENDS test_qsbr_ptr test_qsbr test_art test_art_concurrency)
