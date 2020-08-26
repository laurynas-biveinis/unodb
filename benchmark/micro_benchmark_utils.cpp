// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include "micro_benchmark_utils.hpp"

#include <cassert>
#include <cstdint>
#include <random>
#include <vector>

namespace {

// NOLINTNEXTLINE(fuchsia-statically-constructed-objects,cert-err58-cpp)
std::random_device rd;
// NOLINTNEXTLINE(fuchsia-statically-constructed-objects,cert-err58-cpp)
std::mt19937 gen{rd()};

inline auto rnd_even_0_8() {
  static std::uniform_int_distribution<std::uint8_t> random_key_dist{0, 4ULL};

  const auto result = static_cast<std::uint8_t>(random_key_dist(gen) * 2);
  assert(result <= 8);
  return result;
}

inline auto min_node16_over_dense_node4_lead_key_byte(std::uint8_t i) {
  assert(i < 5);
  return (i < 4) ? static_cast<std::uint8_t>(i * 2 + 1) : rnd_even_0_8();
}

}  // namespace

namespace unodb::benchmark {

std::vector<unodb::key> generate_random_minimal_node16_over_dense_node4_keys(
    unodb::key key_limit) noexcept {
  std::vector<unodb::key> result;
  union {
    std::uint64_t as_int;                  // cppcheck-suppress shadowVariable
    std::array<std::uint8_t, 8> as_bytes;  // cppcheck-suppress shadowVariable
  } key;

  for (std::uint8_t i = 0; i < 5; ++i) {
    key.as_bytes[7] = min_node16_over_dense_node4_lead_key_byte(i);
    for (std::uint8_t i2 = 0; i2 < 5; ++i2) {
      key.as_bytes[6] = min_node16_over_dense_node4_lead_key_byte(i2);
      for (std::uint8_t i3 = 0; i3 < 5; ++i3) {
        key.as_bytes[5] = min_node16_over_dense_node4_lead_key_byte(i3);
        for (std::uint8_t i4 = 0; i4 < 5; ++i4) {
          key.as_bytes[4] = min_node16_over_dense_node4_lead_key_byte(i4);
          for (std::uint8_t i5 = 0; i5 < 5; ++i5) {
            key.as_bytes[3] = min_node16_over_dense_node4_lead_key_byte(i5);
            for (std::uint8_t i6 = 0; i6 < 5; ++i6) {
              key.as_bytes[2] = min_node16_over_dense_node4_lead_key_byte(i6);
              for (std::uint8_t i7 = 0; i7 < 5; ++i7) {
                key.as_bytes[1] = min_node16_over_dense_node4_lead_key_byte(i7);
                key.as_bytes[0] = rnd_even_0_8();
                const unodb::key k = key.as_int;
                if (k > key_limit) {
                  result.shrink_to_fit();
                  std::shuffle(result.begin(), result.end(), gen);
                  return result;
                }
                result.push_back(k);
              }
            }
          }
        }
      }
    }
  }
  cannot_happen();
}

}  // namespace unodb::benchmark
