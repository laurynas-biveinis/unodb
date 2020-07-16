# Copyright 2020 Laurynas Biveinis

function(ADD_BENCHMARK_TARGET TARGET)
  add_executable("${TARGET}" "${TARGET}.cpp" micro_benchmark.hpp)
  common_target_properties("${TARGET}")
  target_include_directories("${TARGET}" SYSTEM PRIVATE
    ${benchmark_include_dirs})
  target_link_libraries("${TARGET}" PRIVATE benchmark::benchmark)
  target_link_libraries("${TARGET}" PRIVATE unodb)
endfunction()

add_benchmark_target(micro_benchmark_key_prefix)
set(micro_benchmark_key_prefix_quick_arg "") # Benchmark is quick as-is
add_benchmark_target(micro_benchmark_node4)
set(micro_benchmark_node4_quick_arg "--benchmark_filter='/100|/16'")
add_benchmark_target(micro_benchmark)
set(micro_benchmark_quick_arg "--benchmark_filter='.*262144|.*51200'")
add_benchmark_target(micro_benchmark_mutex)
set(micro_benchmark_mutex_quick_arg "--benchmark_filter='/4/70000/'")

add_custom_target(benchmarks
  env ${ASAN_ENV} ${UBSAN_ENV} ./micro_benchmark_key_prefix
  COMMAND env ${ASAN_ENV} ${UBSAN_ENV} ./micro_benchmark_node4
  COMMAND env ${ASAN_ENV} ${UBSAN_ENV} ./micro_benchmark
  COMMAND env ${ASAN_ENV} ${UBSAN_ENV} ./micro_benchmark_mutex
  DEPENDS micro_benchmark_key_prefix micro_benchmark_node4 micro_benchmark
  micro_benchmark_mutex)

add_custom_target(quick_benchmarks
  env ${ASAN_ENV} ${UBSAN_ENV}
  ./micro_benchmark_key_prefix ${micro_benchmark_key_prefix_quick_arg}
  COMMAND env ${ASAN_ENV} ${UBSAN_ENV}
  ./micro_benchmark_node4 ${micro_benchmark_node4_quick_arg}
  COMMAND env ${ASAN_ENV} ${UBSAN_ENV}
  ./micro_benchmark ${micro_benchmark_quick_arg}
  COMMAND env ${ASAN_ENV} ${UBSAN_ENV}
  ./micro_benchmark_mutex ${micro_benchmark_mutex_quick_arg}
  DEPENDS micro_benchmark_key_prefix micro_benchmark micro_benchmark_node4
  micro_benchmark_mutex)

add_custom_target(valgrind_benchmarks
  COMMAND valgrind --error-exitcode=1 --leak-check=full
  ./micro_benchmark_key_prefix ${micro_benchmark_key_prefix_quick_arg};
  COMMAND valgrind --error-exitcode=1 --leak-check=full
  ./micro_benchmark_node4 ${micro_benchmark_node4_quick_arg};
  COMMAND valgrind --error-exitcode=1 --leak-check=full
  ./micro_benchmark ${micro_benchmark_quick_arg};
  COMMAND valgrind --error-exitcode=1 --leak-check=full
  ./micro_benchmark_mutex ${micro_benchmark_mutex_quick_arg};
  DEPENDS micro_benchmark_key_prefix
  micro_benchmark_node4 micro_benchmark micro_benchmark_mutex)
