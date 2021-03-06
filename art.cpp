// Copyright 2019-2021 Laurynas Biveinis

#include "global.hpp"

#include "art.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <utility>

#include "art_internal_impl.hpp"
#include "critical_section_unprotected.hpp"

namespace {

template <class INode>
struct inode_pool_getter {
  [[nodiscard]] static inline auto &get() {
    static unodb::detail::pmr_unsynchronized_pool_resource inode_pool{
        unodb::detail::get_inode_pool_options<INode>()};
    return inode_pool;
  }
};

using art_policy = unodb::detail::basic_art_policy<
    unodb::db, unodb::critical_section_unprotected, unodb::detail::node_ptr,
    unodb::detail::basic_db_leaf_deleter, inode_pool_getter>;

using inode_base = unodb::detail::basic_inode_impl<art_policy>;

using delete_db_node_ptr_at_scope_exit =
    art_policy::delete_db_node_ptr_at_scope_exit;

}  // namespace

namespace unodb::detail {

class inode : public inode_base {};

class inode_4 final : public basic_inode_4<art_policy> {
 public:
  using basic_inode_4::basic_inode_4;
};

static_assert(sizeof(inode_4) == 48);

class inode_16 final : public basic_inode_16<art_policy> {
 public:
  using basic_inode_16::basic_inode_16;
};

static_assert(sizeof(inode_16) == 160);

class inode_48 final : public basic_inode_48<art_policy> {
 public:
  using basic_inode_48::basic_inode_48;
};

static_assert(sizeof(inode_48) == 656);

class inode_256 final : public basic_inode_256<art_policy> {
 public:
  using basic_inode_256::basic_inode_256;
};

static_assert(sizeof(inode_256) == 2064);

}  // namespace unodb::detail

namespace {

using leaf = unodb::detail::basic_leaf<unodb::detail::node_header>;

}  // namespace

namespace unodb {

db::~db() noexcept { delete_root_subtree(); }

get_result db::get(key search_key) const noexcept {
  if (unlikely(root.header == nullptr)) return {};

  auto node{root};
  const detail::art_key k{search_key};
  auto remaining_key{k};

  while (true) {
    const auto node_type = node.type();
    if (node_type == detail::node_type::LEAF) {
      if (leaf::matches(node.leaf, k)) {
        const auto value = leaf::value(node.leaf);
        return value;
      }
      return {};
    }

    assert(node_type != detail::node_type::LEAF);

    const auto key_prefix_length = node.internal->key_prefix_length();
    if (node.internal->get_shared_key_prefix_length(remaining_key) <
        key_prefix_length)
      return {};
    remaining_key.shift_right(key_prefix_length);
    auto *const child =
        node.internal->find_child(node_type, remaining_key[0]).second;
    if (child == nullptr) return {};

    node = *child;
    remaining_key.shift_right(1);
  }
}

bool db::insert(key insert_key, value_view v) {
  const auto k = detail::art_key{insert_key};

  if (unlikely(root.header == nullptr)) {
    auto leaf = art_policy::make_db_leaf_ptr(k, v, *this);
    root = detail::node_ptr{leaf.release()};
    return true;
  }

  auto *node = &root;
  detail::tree_depth depth{};
  auto remaining_key{k};

  while (true) {
    const auto node_type = node->type();
    if (node_type == detail::node_type::LEAF) {
      const auto existing_key = leaf::key(node->leaf);
      if (unlikely(k == existing_key)) return false;

      auto leaf = art_policy::make_db_leaf_ptr(k, v, *this);
      increase_memory_use(sizeof(detail::inode_4));
      // TODO(laurynas): try to pass leaf node type instead of generic node
      // below. This way it would be apparent that its key prefix does not need
      // updating as leaves don't have any.
      auto new_node = detail::inode_4::create(existing_key, remaining_key,
                                              depth, *node, std::move(leaf));
      *node = detail::node_ptr{new_node.release()};
      ++inode4_count;
      ++created_inode4_count;
      assert(created_inode4_count >= inode4_count);
      return true;
    }

    assert(node_type != detail::node_type::LEAF);
    assert(depth < detail::art_key::size);

    const auto key_prefix_length = node->internal->key_prefix_length();
    const auto shared_prefix_len =
        node->internal->get_shared_key_prefix_length(remaining_key);
    if (shared_prefix_len < key_prefix_length) {
      auto leaf = art_policy::make_db_leaf_ptr(k, v, *this);
      increase_memory_use(sizeof(detail::inode_4));
      auto new_node = detail::inode_4::create(*node, shared_prefix_len, depth,
                                              std::move(leaf));
      *node = detail::node_ptr{new_node.release()};
      ++inode4_count;
      ++created_inode4_count;
      ++key_prefix_splits;
      assert(created_inode4_count >= inode4_count);
      assert(created_inode4_count > key_prefix_splits);
      return true;
    }

    assert(shared_prefix_len == key_prefix_length);
    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    auto *const child =
        node->internal->find_child(node_type, remaining_key[0]).second;

    if (child == nullptr) {
      auto leaf = art_policy::make_db_leaf_ptr(k, v, *this);

      const auto node_is_full = node->internal->is_full();

      if (likely(!node_is_full)) {
        node->internal->add(std::move(leaf), depth);
        return true;
      }

      assert(node_is_full);

      if (node_type == detail::node_type::I4) {
        assert(inode4_count > 0);

        increase_memory_use(sizeof(detail::inode_16) - sizeof(detail::inode_4));
        std::unique_ptr<detail::inode_4> current_node{node->node_4};
        auto larger_node = detail::inode_16::create(std::move(current_node),
                                                    std::move(leaf), depth);
        *node = detail::node_ptr{larger_node.release()};

        --inode4_count;
        ++inode16_count;
        ++inode4_to_inode16_count;
        assert(inode4_to_inode16_count >= inode16_count);

      } else if (node_type == detail::node_type::I16) {
        assert(inode16_count > 0);

        std::unique_ptr<detail::inode_16> current_node{node->node_16};
        increase_memory_use(sizeof(detail::inode_48) -
                            sizeof(detail::inode_16));
        auto larger_node = detail::inode_48::create(std::move(current_node),
                                                    std::move(leaf), depth);
        *node = detail::node_ptr{larger_node.release()};

        --inode16_count;
        ++inode48_count;
        ++inode16_to_inode48_count;
        assert(inode16_to_inode48_count >= inode48_count);

      } else {
        assert(inode48_count > 0);

        assert(node_type == detail::node_type::I48);
        std::unique_ptr<detail::inode_48> current_node{node->node_48};
        increase_memory_use(sizeof(detail::inode_256) -
                            sizeof(detail::inode_48));
        auto larger_node = detail::inode_256::create(std::move(current_node),
                                                     std::move(leaf), depth);
        *node = detail::node_ptr{larger_node.release()};

        --inode48_count;
        ++inode256_count;
        ++inode48_to_inode256_count;
        assert(inode48_to_inode256_count >= inode256_count);
      }
      return true;
    }

    node = reinterpret_cast<detail::node_ptr *>(child);
    ++depth;
    remaining_key.shift_right(1);
  }
}

bool db::remove(key remove_key) {
  const auto k = detail::art_key{remove_key};

  if (unlikely(root == nullptr)) return false;

  if (root.type() == detail::node_type::LEAF) {
    if (leaf::matches(root.leaf, k)) {
      // FIXME(laurynas): why not same in node deleters?
      const auto delete_root_leaf_on_scope_exit{
          art_policy::make_db_reclaimable_leaf_ptr(root.leaf, *this)};
      root = nullptr;
      return true;
    }
    return false;
  }

  auto *node = &root;
  detail::tree_depth depth{};
  auto remaining_key{k};

  while (true) {
    const auto node_type = node->type();
    assert(node_type != detail::node_type::LEAF);
    assert(depth < detail::art_key::size);

    const auto key_prefix_length = node->internal->key_prefix_length();
    const auto shared_prefix_len =
        node->internal->get_shared_key_prefix_length(remaining_key);
    if (shared_prefix_len < key_prefix_length) return false;

    assert(shared_prefix_len == key_prefix_length);
    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    const auto [child_i, child_ptr] =
        node->internal->find_child(node_type, remaining_key[0]);

    if (child_ptr == nullptr) return false;

    if (child_ptr->load().type() == detail::node_type::LEAF) {
      if (!leaf::matches(child_ptr->load().leaf, k)) return false;

      const auto is_node_min_size = node->internal->is_min_size();

      if (likely(!is_node_min_size)) {
        node->internal->remove(child_i, *this);
        return true;
      }

      assert(is_node_min_size);

      if (node_type == detail::node_type::I4) {
        std::unique_ptr<detail::inode_4> current_node{node->node_4};
        *node = current_node->leave_last_child(child_i, *this);
        decrease_memory_use(sizeof(detail::inode_4));

        assert(inode4_count > 0);
        --inode4_count;
        ++deleted_inode4_count;
        assert(deleted_inode4_count <= created_inode4_count);

      } else if (node_type == detail::node_type::I16) {
        std::unique_ptr<detail::inode_16> current_node{node->node_16};
        auto new_node{
            detail::inode_4::create(std::move(current_node), child_i, *this)};
        *node = detail::node_ptr{new_node.release()};
        decrease_memory_use(sizeof(detail::inode_16) - sizeof(detail::inode_4));

        assert(inode16_count > 0);
        --inode16_count;
        ++inode4_count;
        ++inode16_to_inode4_count;
        assert(inode16_to_inode4_count <= inode4_to_inode16_count);

      } else if (node_type == detail::node_type::I48) {
        std::unique_ptr<detail::inode_48> current_node{node->node_48};
        auto new_node{
            detail::inode_16::create(std::move(current_node), child_i, *this)};
        *node = detail::node_ptr{new_node.release()};
        decrease_memory_use(sizeof(detail::inode_48) -
                            sizeof(detail::inode_16));

        assert(inode48_count > 0);
        --inode48_count;
        ++inode16_count;
        ++inode48_to_inode16_count;
        assert(inode48_to_inode16_count <= inode16_to_inode48_count);

      } else {
        assert(node_type == detail::node_type::I256);
        std::unique_ptr<detail::inode_256> current_node{node->node_256};
        auto new_node{
            detail::inode_48::create(std::move(current_node), child_i, *this)};
        *node = detail::node_ptr{new_node.release()};
        decrease_memory_use(sizeof(detail::inode_256) -
                            sizeof(detail::inode_48));

        assert(inode256_count > 0);
        --inode256_count;
        ++inode48_count;
        ++inode256_to_inode48_count;
        assert(inode256_to_inode48_count <= inode48_to_inode256_count);
      }

      return true;
    }

    node = reinterpret_cast<detail::node_ptr *>(child_ptr);
    ++depth;
    remaining_key.shift_right(1);
  }
}

void db::delete_subtree(unodb::detail::node_ptr node) noexcept {
  delete_db_node_ptr_at_scope_exit delete_on_scope_exit(node, *this);
  delete_on_scope_exit.delete_subtree();
}

void db::delete_root_subtree() noexcept {
  delete_subtree(root);

  // It is possible to reset the counter to zero instead of decrementing it for
  // each leaf, but not sure the savings will be significant.
  assert(leaf_count == 0);
}

void db::clear() {
  delete_root_subtree();

  root = nullptr;
  current_memory_use = 0;
  inode4_count = 0;
  inode16_count = 0;
  inode48_count = 0;
  inode256_count = 0;
}

void db::dump(std::ostream &os) const {
  os << "db dump, current memory use = " << get_current_memory_use() << '\n';
  detail::dump_node(os, root);
}

}  // namespace unodb
