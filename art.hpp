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

enum class node_type : uint8_t { LEAF };

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

  [[nodiscard]] static auto is_leaf(single_value_leaf::type leaf) {
    // Could be "return true;" and the whole thing looks like
    // single_value_leaf::is_leaf but bear with me
    return leaf[offset_type] == std::byte{node_type::LEAF};
  }

  [[nodiscard]] static bool matches(single_value_leaf::type leaf,
                                    key_type k) noexcept {
    return !memcmp(&leaf[offset_key], &k, sizeof(k));
  }

  [[nodiscard]] static auto value(single_value_leaf::type leaf) noexcept {
    return value_view(&leaf[offset_value], value_size(leaf));
  }

 private:
  // Non-owning pointer to somewhere middle of the node
  // Use std::observer_ptr<std::byte> once it's available
  using field_ptr = std::byte *;

  using value_size_type = uint32_t;

  static const constexpr auto offset_type = 0;
  static const constexpr auto offset_key = sizeof(node_type);
  static const constexpr auto offset_value_size = offset_key + sizeof(key_type);
  static const constexpr auto offset_value =
      offset_value_size + sizeof(value_size_type);

  static const constexpr auto minimum_size = offset_value;

  [[nodiscard]] static size_t size(single_value_leaf::type leaf) noexcept {
    return value_size(leaf) + offset_value;
  }

  [[nodiscard]] static value_size_type value_size(
      single_value_leaf::type leaf) noexcept {
    value_size_type result;
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
  single_value_leaf::unique_ptr root;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
