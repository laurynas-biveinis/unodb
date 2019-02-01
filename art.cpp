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

single_value_leaf::unique_ptr single_value_leaf::make(key_type k,
                                                      value_view v) {
  static_assert(sizeof(decltype(v.size())) == 8);
  const auto leaf_size = sizeof(k) + 8 + static_cast<size_t>(v.size());
  assert(leaf_size >= minimum_size);
  auto *const leaf_mem = static_cast<std::byte *>(
      boost::container::pmr::new_delete_resource()->allocate(leaf_size));
  memcpy(leaf_mem, &k, sizeof(k));
  memcpy(leaf_mem + sizeof(k), &leaf_size, 8);
  if (!v.empty())
    memcpy(value_ptr(leaf_mem), &v[0], static_cast<size_t>(v.size()));
  return single_value_leaf::unique_ptr(leaf_mem);
}

db::get_result db::get(key_type k) noexcept {
  if (!root) return get_result{};
  const auto bin_comparable_key = make_binary_comparable(k);
  if (root.is_leaf()) {
    const auto &root_leaf = root.get_leaf();
    if (single_value_leaf::matches(root_leaf, bin_comparable_key)) {
      const auto value_view = single_value_leaf::value(root_leaf);
      return get_result{std::in_place, value_view.cbegin(), value_view.cend()};
    } else {
      return get_result{};
    }
  }
  assert(0);
  return get_result{};
}

void db::insert(key_type k, value_view v) {
  const auto bin_comparable_key = make_binary_comparable(k);
  auto leaf_node = single_value_leaf::make(bin_comparable_key, v);
  if (!root) root = root_node::create_leaf(std::move(leaf_node));
}

}  // namespace unodb
