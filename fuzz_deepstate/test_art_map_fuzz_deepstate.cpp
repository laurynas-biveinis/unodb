// Copyright (C) 2021 Laurynas Biveinis
#include "global.hpp"

#include <array>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <deepstate/DeepState.hpp>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "art_map_db.hpp"
#include "deepstate_utils.hpp"

namespace {

// Close to the longest test run that fits into 8192 random bytes provided by
// DeepState API.
constexpr auto test_length = 480;

auto get_value() {
  static constexpr auto val{std::array<std::byte, 8>{}};
  return val;
}

#if 0
auto dump_value(const unodb::value_view &value) {
  for (auto b : value) {
    unodb::detail::dump_byte(LOG(TRACE), b);
  }
}
#endif

unodb::key get_key(unodb::key max_key_value,
                   const std::vector<unodb::key> &keys) {
  const auto use_existing_key = !keys.empty() && DeepState_Bool();
  if (use_existing_key) {
    ASSERT(!keys.empty());
    ASSERT(keys.size() <= std::numeric_limits<std::uint32_t>::max());
    const auto existing_key_i = DeepState_ContainerIndex(keys);
    return keys[existing_key_i];
  }
  return DeepState_UInt64InRange(0, max_key_value);
}

void dump_tree(const unodb::art_map_db &tree) {
  // Dump the tree to a string. Do not attempt to check the dump format, only
  // that dumping does not crash
  std::stringstream dump_sink;
  tree.dump(dump_sink);
}

}  // namespace

UNODB_START_DEEPSTATE_TESTS()

TEST(ART_MAP, DeepStateFuzz) {
  const auto limit_max_key = DeepState_Bool();
  const auto max_key_value =
      limit_max_key
      ? DeepState_UInt64InRange(0, std::numeric_limits<unodb::key>::max())
      : std::numeric_limits<unodb::key>::max();
  if (limit_max_key)
    LOG(TRACE) << "Limiting maximum key value to " << max_key_value;
  else
    LOG(TRACE) << "Not limiting maximum key value (" << max_key_value << ")";

  unodb::art_map_db test_db;
  ASSERT(test_db.empty());

  std::vector<unodb::key> keys;
  std::unordered_map<unodb::key, unodb::value_view> oracle;

  for (auto i = 0; i < test_length; i++) {
    LOG(TRACE) << "Iteration " << i;
    deepstate::OneOf(
        // Insert
        [&] {
          const auto key = DeepState_UInt64InRange(0, max_key_value);
          const auto value = get_value();
          const auto mem_use_before = test_db.get_current_memory_use();
          try {
            const auto insert_result = test_db.insert(key, value);
            const auto mem_use_after = test_db.get_current_memory_use();
            if (insert_result) {
              LOG(TRACE) << "Inserted key " << key;
              ASSERT(!test_db.empty());
              ASSERT(mem_use_after > mem_use_before);
              const auto oracle_insert_result = oracle.emplace(key, value);
              ASSERT(oracle_insert_result.second)
                  << "If insert suceeded, oracle insert must succeed";
              keys.emplace_back(key);
            } else {
              LOG(TRACE) << "Tried to insert duplicate key " << key;
              ASSERT(mem_use_after == mem_use_before);
              ASSERT(oracle.find(key) != oracle.cend())
                  << "If insert returned failure, oracle must contain that "
                  "value";
            }
          } catch (const std::bad_alloc &) {
            const auto mem_use_after = test_db.get_current_memory_use();
            ASSERT(mem_use_after == mem_use_before);
          }
          dump_tree(test_db);
          LOG(TRACE) << "Current mem use: " << test_db.get_current_memory_use();
        },
        // Query
        [&] {
          const auto key = get_key(max_key_value, keys);
          LOG(TRACE) << "Searching for key " << key;
          const auto search_result = test_db.get(key);
          const auto oracle_search_result = oracle.find(key);
          if (search_result) {
            ASSERT(!test_db.empty());
            ASSERT(oracle_search_result != oracle.cend())
                << "If search for a key returned a value, "
                "oracle must contain that key";
          } else {
            ASSERT(oracle_search_result == oracle.cend())
                << "If search for a key did not find a value, oracle must not "
                " contain that key";
          }
        },
        // Delete
        [&] {
          // Delete everything with 0.1% probability
          const auto clear = (DeepState_UIntInRange(0, 999) == 0);
          if (clear) {
            LOG(TRACE) << "Clearing the tree";
            test_db.clear();
            oracle.clear();
            ASSERT(test_db.get_current_memory_use() == 0);
            ASSERT(test_db.empty());
            return;
          }
          const auto key = get_key(max_key_value, keys);
          LOG(TRACE) << "Deleting key " << key;
          const auto mem_use_before = test_db.get_current_memory_use();
          const auto delete_result = test_db.remove(key);
          const auto mem_use_after = test_db.get_current_memory_use();
          const auto oracle_delete_result = oracle.erase(key);
          if (delete_result) {
            ASSERT(mem_use_after < mem_use_before);
            ASSERT(oracle_delete_result == 1)
                << "If delete succeeded, oracle delete must succeed too";
          } else {
            ASSERT(mem_use_after == mem_use_before);
            ASSERT(oracle_delete_result == 0)
                << "If delete failed, oracle delete must fail too";
          }
          dump_tree(test_db);
          LOG(TRACE) << "Current mem use: " << test_db.get_current_memory_use();
        });
  }

  auto prev_mem_use = test_db.get_current_memory_use();
  while (!oracle.empty()) {
    const auto [key, value] = *oracle.cbegin();
    LOG(TRACE) << "Shutdown: deleting key " << key;
    const auto oracle_remove_result = oracle.erase(key);
    ASSERT(oracle_remove_result == 1);
    const auto db_remove_result = test_db.remove(key);
    ASSERT(db_remove_result);
    const auto current_mem_use = test_db.get_current_memory_use();
    LOG(TRACE) << "Current mem use: " << current_mem_use;
    ASSERT(current_mem_use < prev_mem_use);
    prev_mem_use = current_mem_use;
  }
  ASSERT(prev_mem_use == 0);
  ASSERT(test_db.empty());
}
