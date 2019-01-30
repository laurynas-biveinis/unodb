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
  db() noexcept : leaf_mem_pool(boost::container::pmr::new_delete_resource()) {}
  void insert(key_type k, value_type v);

 private:
  using single_value_leaf = std::byte *;

  [[nodiscard]] std::unique_ptr<single_value_leaf> make_single_value_leaf(
      key_type k, value_type v);

  boost::container::pmr::memory_resource *leaf_mem_pool;
  std::unique_ptr<single_value_leaf> root;
};

}  // namespace unodb

#endif  // UNODB_ART_HPP_
