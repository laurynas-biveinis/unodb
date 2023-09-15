// Copyright (C) 2019-2024 Laurynas Biveinis

#include "global.hpp"

// IWYU pragma: no_include <string>

#include <benchmark/benchmark.h>

#include "micro_benchmark_concurrency.hpp"
#include "micro_benchmark_utils.hpp"
#include "olc_art.hpp"
#include "qsbr.hpp"

namespace {

class [[nodiscard]] concurrent_benchmark_olc final
    : public unodb::benchmark::concurrent_benchmark<unodb::olc_db,
                                                    unodb::qsbr_thread> {
 private:
  void setup() override {
    unodb::qsbr::instance().assert_idle();
    unodb::qsbr::instance().reset_stats();
  }

  void end_workload_in_main_thread() override {
    unodb::this_thread().quiescent();
  }

  void teardown() noexcept override { unodb::qsbr::instance().assert_idle(); }
};

concurrent_benchmark_olc benchmark_fixture;

void set_common_qsbr_counters(benchmark::State &state) {
  state.counters["epoch changes"] = unodb::benchmark::to_counter(
      unodb::qsbr::instance().get_epoch_change_count());
  state.counters["mean qstates before epoch change"] = benchmark::Counter(
      unodb::qsbr::instance()
          .get_mean_quiescent_states_per_thread_between_epoch_changes());
}

void parallel_get(benchmark::State &state) {
  benchmark_fixture.parallel_get(state);

  set_common_qsbr_counters(state);
}

void parallel_insert_disjoint_ranges(benchmark::State &state) {
  benchmark_fixture.parallel_insert_disjoint_ranges(state);

  state.counters["QSBR callback count max"] = unodb::benchmark::to_counter(
      unodb::qsbr::instance().get_epoch_callback_count_max());
  state.counters["callback count variance"] = benchmark::Counter(
      unodb::qsbr::instance().get_epoch_callback_count_variance());
  set_common_qsbr_counters(state);
}

void parallel_delete_disjoint_ranges(benchmark::State &state) {
  benchmark_fixture.parallel_delete_disjoint_ranges(state);

  state.counters["max backlog bytes"] = unodb::benchmark::to_counter(
      unodb::qsbr::instance().get_max_backlog_bytes());
  state.counters["mean backlog bytes"] =
      benchmark::Counter(unodb::qsbr::instance().get_mean_backlog_bytes());
  set_common_qsbr_counters(state);
}

}  // namespace

UNODB_START_BENCHMARKS()

BENCHMARK(parallel_get)
    ->Apply(unodb::benchmark::concurrency_ranges16)
    ->Unit(benchmark::kMillisecond)
    ->MeasureProcessCPUTime()
    ->UseRealTime();
BENCHMARK(parallel_insert_disjoint_ranges)
    ->Apply(unodb::benchmark::concurrency_ranges32)
    ->Unit(benchmark::kMillisecond)
    ->MeasureProcessCPUTime()
    ->UseRealTime();
BENCHMARK(parallel_delete_disjoint_ranges)
    ->Apply(unodb::benchmark::concurrency_ranges32)
    ->Unit(benchmark::kMillisecond)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

UNODB_BENCHMARK_MAIN();
