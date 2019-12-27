// Copyright 2019 Laurynas Biveinis

#ifndef NDEBUG
#include <sstream>
#endif
#include <unordered_map>

#include <deepstate/DeepState.hpp>

#include "art.hpp"

namespace {

constexpr auto maximum_art_mem = 1024 * 1024 * 128;  // 128MB
constexpr auto maximum_value_len = 1024 * 1024;      // 1MB
// Close to the longest test run that fits into 8192 random bytes provided by
// DeepState API.
constexpr auto test_length = 480;

using dynamic_value = std::vector<std::byte>;

using values_type = std::vector<dynamic_value>;

auto make_random_value(dynamic_value::size_type length) {
  dynamic_value result{length};
  for (dynamic_value::size_type i = 0; i < length; i++) {
    // Ideally we would take random bytes from DeepState, but we'd end up
    // exhausting their default source len too soon. Do something deterministic
    // that has embedded zero bytes to shake out any C string API use
    result[i] = gsl::narrow_cast<std::byte>(i % 256);
  }
  return result;
}

auto get_value(dynamic_value::size_type max_length, values_type &values) {
  const auto make_new_value = values.empty() || DeepState_Bool();
  ASSERT(max_length <= std::numeric_limits<uint32_t>::max());
  if (make_new_value) {
    const auto new_value_len = static_cast<dynamic_value::size_type>(
        DeepState_UIntInRange(0, static_cast<uint32_t>(max_length)));
    auto new_value = make_random_value(new_value_len);
    LOG(TRACE) << "Making a new value of length "
               << static_cast<uint64_t>(new_value_len);
    const auto &inserted_value = values.emplace_back(std::move(new_value));
    return unodb::value_view{inserted_value};
  }
  LOG(TRACE) << "Reusing an existing value";
  ASSERT(values.size() <= std::numeric_limits<uint32_t>::max());
  const auto existing_value_i = static_cast<values_type::size_type>(
      DeepState_UIntInRange(0, static_cast<uint32_t>(values.size() - 1)));
  const auto &existing_value = values[existing_value_i];
  return unodb::value_view{existing_value};
}

unodb::key get_key(unodb::key max_key_value,
                   const std::vector<unodb::key> &keys) {
  const auto use_existing_key = !keys.empty() && DeepState_Bool();
  if (use_existing_key) {
    ASSERT(!keys.empty());
    ASSERT(keys.size() <= std::numeric_limits<uint32_t>::max());
    const auto existing_key_i = static_cast<std::size_t>(
        DeepState_UIntInRange(0, static_cast<uint32_t>(keys.size()) - 1));
    return keys[existing_key_i];
  }
  return DeepState_UInt64InRange(0, max_key_value);
}

void dump_tree(const unodb::db &tree) {
#ifndef NDEBUG
  // Dump the tree to a string. Do not attempt to check the dump format, only
  // that dumping does not crash
  std::stringstream dump_sink;
  tree.dump(dump_sink);
#else
  (void)tree;
#endif
}

}  // namespace

// warning: function 'DeepState_Run_ART_DeepState_fuzz' could be declared with
// attribute 'noreturn' [-Wmissing-noreturn]
// We consider this to be a TEST macro internal implementation detail
DISABLE_CLANG_WARNING("-Wmissing-noreturn")

TEST(ART, DeepState_fuzz) {
  const auto mem_limit =
      static_cast<std::size_t>(DeepState_IntInRange(0, maximum_art_mem));
  LOG(TRACE) << "ART memory limit is " << static_cast<uint64_t>(mem_limit);

  const auto limit_max_key = DeepState_Bool();
  const auto max_key_value =
      limit_max_key
          ? DeepState_UInt64InRange(0, std::numeric_limits<unodb::key>::max())
          : std::numeric_limits<unodb::key>::max();
  if (limit_max_key)
    LOG(TRACE) << "Limiting maximum key value to "
               << static_cast<uint64_t>(max_key_value);
  else
    LOG(TRACE) << "Not limiting maximum key value (" << max_key_value << ")";

  static_assert(maximum_value_len <= std::numeric_limits<uint32_t>::max());
  const auto limit_value_length = DeepState_Bool();
  const auto max_value_length =
      limit_value_length ? DeepState_UIntInRange(0, maximum_value_len)
                         : maximum_value_len;
  if (limit_value_length)
    LOG(TRACE) << "Limiting maximum value length to " << max_value_length;
  else
    LOG(TRACE) << "Not limiting value length (" << max_value_length << ")";

  unodb::db test_db{mem_limit};
  ASSERT(test_db.empty());

  std::vector<unodb::key> keys;
  values_type values;
  std::unordered_map<unodb::key, unodb::value_view> oracle;

  for (auto i = 0; i < test_length; i++) {
    LOG(TRACE) << "Iteration " << i;
    deepstate::OneOf(
        // Insert
        [&] {
          const auto key = DeepState_UInt64InRange(0, max_key_value);
          const auto value = get_value(max_value_length, values);
          const auto mem_use_before = test_db.get_current_memory_use();
          try {
            const auto insert_result = test_db.insert(key, value);
            const auto mem_use_after = test_db.get_current_memory_use();
            if (insert_result) {
              LOG(TRACE) << "Inserted key " << key;
              ASSERT(!test_db.empty());
              ASSERT(mem_use_after > mem_use_before || mem_limit == 0);
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
          LOG(TRACE) << "Current mem use: "
                     << static_cast<uint64_t>(test_db.get_current_memory_use());
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
                << "If search returned a value, oracle must contain that value";
            ASSERT(std::equal(search_result->cbegin(), search_result->cend(),
                              oracle_search_result->second.cbegin(),
                              oracle_search_result->second.cend()))
                << "Values stored in ART and in oracle must match";
          } else {
            ASSERT(oracle_search_result == oracle.cend())
                << "If search did not find a value, oracle must not contain "
                   "that value";
          }
        },
        // Delete
        [&] {
          const auto key = get_key(max_key_value, keys);
          LOG(TRACE) << "Deleting key " << key;
          const auto mem_use_before = test_db.get_current_memory_use();
          const auto delete_result = test_db.remove(key);
          const auto mem_use_after = test_db.get_current_memory_use();
          const auto oracle_delete_result = oracle.erase(key);
          if (delete_result) {
            ASSERT(mem_use_after < mem_use_before || mem_limit == 0);
            ASSERT(oracle_delete_result == 1)
                << "If delete succeeded, oracle delete must succeed too";
          } else {
            ASSERT(mem_use_after == mem_use_before);
            ASSERT(oracle_delete_result == 0)
                << "If delete failed, oracle delete must fail too";
          }
          dump_tree(test_db);
          LOG(TRACE) << "Current mem use: "
                     << static_cast<uint64_t>(test_db.get_current_memory_use());
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
    LOG(TRACE) << "Current mem use: " << static_cast<uint64_t>(current_mem_use);
    ASSERT(current_mem_use < prev_mem_use || mem_limit == 0);
    prev_mem_use = current_mem_use;
  }
  ASSERT(prev_mem_use == 0);
  ASSERT(test_db.empty());
}

RESTORE_CLANG_WARNINGS()
