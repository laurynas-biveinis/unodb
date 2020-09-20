// Copyright 2020 Laurynas Biveinis

#include "global.hpp"

#include "micro_benchmark_utils.hpp"

#include <cassert>
#include <cstdint>
#include <random>
#include <vector>

namespace unodb::benchmark {

std::vector<unodb::key> generate_random_minimal_node16_over_dense_node4_keys(
    unodb::key key_limit) noexcept {
  std::uniform_int_distribution<std::uint8_t> random_04{0, 4ULL};

  std::vector<unodb::key> result;
  union {
    std::uint64_t as_int;                  // cppcheck-suppress shadowVariable
    std::array<std::uint8_t, 8> as_bytes;  // cppcheck-suppress shadowVariable
  } key;

  for (std::uint8_t i = 0; i < 4; ++i) {
    key.as_bytes[7] = static_cast<std::uint8_t>(i * 2 + 1);
    for (std::uint8_t i2 = 0; i2 < 4; ++i2) {
      key.as_bytes[6] = static_cast<std::uint8_t>(i2 * 2 + 1);
      for (std::uint8_t i3 = 0; i3 < 4; ++i3) {
        key.as_bytes[5] = static_cast<std::uint8_t>(i3 * 2 + 1);
        for (std::uint8_t i4 = 0; i4 < 4; ++i4) {
          key.as_bytes[4] = static_cast<std::uint8_t>(i4 * 2 + 1);
          for (std::uint8_t i5 = 0; i5 < 4; ++i5) {
            key.as_bytes[3] = static_cast<std::uint8_t>(i5 * 2 + 1);
            for (std::uint8_t i6 = 0; i6 < 4; ++i6) {
              key.as_bytes[2] = static_cast<std::uint8_t>(i6 * 2 + 1);
              for (std::uint8_t i7 = 0; i7 < 4; ++i7) {
                key.as_bytes[1] = static_cast<std::uint8_t>(i7 * 2 + 1);
                key.as_bytes[0] =
                    static_cast<std::uint8_t>(random_04(get_prng()) * 2);

                const unodb::key k = key.as_int;
                if (k > key_limit) {
                  result.shrink_to_fit();
                  std::shuffle(result.begin(), result.end(), get_prng());
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
