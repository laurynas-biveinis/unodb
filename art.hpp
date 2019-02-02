// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_ART_HPP_
#define UNODB_ART_HPP_

#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>  // for uint64_t
#include <cstdint>  // IWYU pragma: keep
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <boost/container/pmr/global_resource.hpp>
#include <boost/container/pmr/memory_resource.hpp>
#include <gsl/span>

namespace unodb {

using key_type = uint64_t;
using value_view = gsl::span<const std::byte>;

// Helper struct for leaf node-related data and (static) code. We
// don't use a regular class because leaf nodes are of variable size
// and we want to save one level of indirection - we want to be able
// to point directly to the node in memory
struct single_value_leaf {
 private:
  // Single value leaf node proper
  using type = std::byte[];

  struct deleter {
    void operator()(single_value_leaf::type to_delete) noexcept {
      const auto s = single_value_leaf::size(to_delete);
      boost::container::pmr::new_delete_resource()->deallocate(to_delete, s);
    }
  };

 public:
  using unique_ptr = std::unique_ptr<type, deleter>;

  [[nodiscard]] static unique_ptr make(key_type k, value_view v);

  [[nodiscard]] static bool matches(single_value_leaf::type leaf,
                                    key_type k) noexcept {
    return !memcmp(&leaf[offset_key], &k, sizeof(k));
  }

  [[nodiscard]] static auto value(single_value_leaf::type leaf) noexcept {
    const auto s = value_size(leaf);
    assert(s <= std::numeric_limits<value_view::index_type>::max());
    return value_view(&leaf[offset_value],
                      static_cast<value_view::index_type>(s));
  }

 private:
  // Non-owning pointer to somewhere middle of the node
  // Use std::observer_ptr<std::byte> once it's available
  using field_ptr = std::byte *;

  static const constexpr auto offset_key = 0;
  static const constexpr auto offset_value_size = offset_key + sizeof(key_type);
  static const constexpr auto offset_value =
      offset_value_size + sizeof(uint64_t);

  static const constexpr auto minimum_size = offset_value;

  [[nodiscard]] static uint64_t size(single_value_leaf::type leaf) noexcept {
    return value_size(leaf) + offset_value;
  }

  [[nodiscard]] static uint64_t value_size(
      single_value_leaf::type leaf) noexcept {
    uint64_t result;
    memcpy(&result, &leaf[offset_value_size], sizeof(result));
    return result;
  }
};

class db {
 public:
  using get_result = std::optional<std::vector<std::byte>>;

  [[nodiscard]] get_result get(key_type k) noexcept;

  void insert(key_type k, value_view v);

 private:
  class root_node {
   public:
    root_node() = default;

    [[nodiscard]] static root_node create_leaf(
        single_value_leaf::unique_ptr new_root) {
      return root_node(true, std::move(new_root));
    }

    [[nodiscard]] auto is_leaf() const noexcept { return leaf; }

    [[nodiscard]] auto get_leaf() const noexcept {
      Expects(is_leaf());
      return root.get();
    }

    [[nodiscard]] auto operator!() const noexcept { return !root; }

   private:
    root_node(bool leaf_, single_value_leaf::unique_ptr root_) noexcept
        : root(std::move(root_)), leaf(leaf_){};

    single_value_leaf::unique_ptr root;

    bool leaf{true};
  };

  root_node root;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
