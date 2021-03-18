// Copyright 2019-2021 Laurynas Biveinis

#include "global.hpp"  // IWYU pragma: keep

#include <cassert>

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
    assert_idle_qsbr();
    unodb::qsbr::instance().reset();
  }

  void end_workload_in_main_thread() override {
    unodb::current_thread_reclamator().quiescent_state();
  }

  void teardown() override { assert_idle_qsbr(); }

 private:
  void assert_idle_qsbr() const {
    // FIXME(laurynas): copy-paste with expect_idle_qsbr, but not clear how to
    // fix this
    assert(unodb::qsbr::instance().single_thread_mode());
    assert(unodb::qsbr::instance().number_of_threads() == 1);
    assert(unodb::qsbr::instance().previous_interval_size() == 0);
    assert(unodb::qsbr::instance().current_interval_size() == 0);
    assert(unodb::qsbr::instance().get_reserved_thread_capacity() == 1);
    assert(unodb::qsbr::instance().get_threads_in_previous_epoch() == 1);
  }
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
