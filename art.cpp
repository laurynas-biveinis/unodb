// Copyright 2019 Laurynas Biveinis
#include "art.hpp"

namespace {

[[nodiscard]] auto make_binary_comparable(const uint64_t key) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(key);
#else
#error Needs implementing
#endif
}

}  // namespace

namespace unodb {

db::leaf_node_unique_ptr db::make_single_value_leaf(key_type k, value_type v) {
  static_assert(sizeof(decltype(v.size())) == 8);
  const auto leaf_size = sizeof(k) + 8 + static_cast<size_t>(v.size());
  auto *const leaf_mem = static_cast<std::byte *>(
      boost::container::pmr::new_delete_resource()->allocate(leaf_size));
  memcpy(leaf_mem, &k, sizeof(k));
  memcpy(leaf_mem + sizeof(k), &leaf_size, 8);
  if (!v.empty())
    memcpy(leaf_mem + sizeof(k) + 8, &v[0], static_cast<size_t>(v.size()));
  return leaf_node_unique_ptr(leaf_mem);
}

void db::insert(key_type k, value_type v) {
  const auto bin_comparable_key = make_binary_comparable(k);
  auto leaf_node = make_single_value_leaf(bin_comparable_key, v);
  if (!root) root = std::move(leaf_node);
}

}  // namespace unodb
