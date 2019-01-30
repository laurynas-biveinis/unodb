// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_ART_HPP_
#define UNODB_ART_HPP_

#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>  // for uint64_t
#include <cstdint>  // IWYU pragma: keep
#include <cstring>
#include <memory>

#include <boost/container/pmr/global_resource.hpp>
#include <boost/container/pmr/memory_resource.hpp>
#include <gsl/span>

namespace unodb {

using key_type = uint64_t;
using value_type = gsl::span<const std::byte>;

class db {
 public:
  void insert(key_type k, value_type v);

 private:
  using single_value_leaf = std::byte[];

  struct leaf_mem_pool_deleter {
    void operator()(single_value_leaf to_delete) noexcept {
      uint64_t size;
      memcpy(&size, to_delete + sizeof(key_type), sizeof(size));
      boost::container::pmr::new_delete_resource()->deallocate(to_delete, size);
    }
  };

  using leaf_node_unique_ptr =
      std::unique_ptr<single_value_leaf, leaf_mem_pool_deleter>;

  [[nodiscard]] leaf_node_unique_ptr make_single_value_leaf(key_type k,
                                                            value_type v);

  leaf_node_unique_ptr root;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
