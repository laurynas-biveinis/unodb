// Copyright 2019-2021 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include <benchmark/benchmark.h>

#include "micro_benchmark_concurrency.hpp"
#include "olc_art.hpp"
#include "qsbr.hpp"

namespace {

class concurrent_benchmark_olc final
    : public unodb::benchmark::concurrent_benchmark<unodb::olc_db,
                                                    unodb::qsbr_thread> {
 protected:
  void setup() override {
    unodb::qsbr::instance().assert_idle();
    unodb::qsbr::instance().reset_stats();
  }

  void end_workload_in_main_thread() override {
    unodb::current_thread_reclamator().quiescent_state();
  }

  void teardown() override { unodb::qsbr::instance().assert_idle(); }
};

concurrent_benchmark_olc benchmark_fixture;

void set_common_qsbr_counters(benchmark::State &state) {
  state.counters["epoch changes"] =
      unodb::benchmark::to_counter(unodb::qsbr::instance().get_current_epoch());
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

BENCHMARK_MAIN();
