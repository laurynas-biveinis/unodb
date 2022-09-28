// Copyright 2019-2022 Laurynas Biveinis

#include "global.hpp"

#include "art.hpp"

#include <iostream>
#include <type_traits>
#include <utility>  // IWYU pragma: keep

#include "art_internal_impl.hpp"
#include "assert.hpp"
#include "in_fake_critical_section.hpp"
#include "node_type.hpp"

namespace unodb::detail {

struct [[nodiscard]] node_header {};

static_assert(std::is_empty_v<node_header>);

}  // namespace unodb::detail

namespace {

class inode;
class inode_4;
class inode_16;
class inode_48;
class inode_256;

using inode_defs = unodb::detail::basic_inode_def<inode, inode_4, inode_16,
                                                  inode_48, inode_256>;

template <class INode>
using db_inode_deleter =
    unodb::detail::basic_db_inode_deleter<INode, unodb::db>;

using art_policy = unodb::detail::basic_art_policy<
    unodb::db, unodb::in_fake_critical_section, unodb::detail::node_ptr,
    inode_defs, db_inode_deleter, unodb::detail::basic_db_leaf_deleter>;

using inode_base = unodb::detail::basic_inode_impl<art_policy>;

using leaf = unodb::detail::basic_leaf<unodb::detail::node_header>;

class inode : public inode_base {};

}  // namespace

namespace unodb::detail {

struct impl_helpers {
  // GCC 10 diagnoses parameters that are present only in uninstantiated if
  // constexpr branch, such as node_in_parent for inode_256.
  UNODB_DETAIL_DISABLE_GCC_10_WARNING("-Wunused-parameter")

  template <class INode>
  [[nodiscard]] static detail::node_ptr *add_or_choose_subtree(
      INode &inode, std::byte key_byte, art_key k, value_view v,
      db &db_instance, tree_depth depth, detail::node_ptr *node_in_parent);

  UNODB_DETAIL_RESTORE_GCC_10_WARNINGS()

  template <class INode>
  [[nodiscard]] static std::optional<detail::node_ptr *>
  remove_or_choose_subtree(INode &inode, std::byte key_byte, detail::art_key k,
                           db &db_instance, detail::node_ptr *node_in_parent);

  impl_helpers() = delete;
};

}  // namespace unodb::detail

namespace {

class [[nodiscard]] inode_4 final
    : public unodb::detail::basic_inode_4<art_policy> {
 public:
  using basic_inode_4::basic_inode_4;

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return unodb::detail::impl_helpers::add_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return unodb::detail::impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }
};

#ifndef _MSC_VER
static_assert(sizeof(inode_4) == 48);
#else
// MSVC pads the first field to 8 byte boundary even though its natural
// alignment is 4 bytes, maybe due to parent class sizeof
static_assert(sizeof(inode_4) == 56);
#endif

class [[nodiscard]] inode_16 final
    : public unodb::detail::basic_inode_16<art_policy> {
 public:
  using basic_inode_16::basic_inode_16;

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return unodb::detail::impl_helpers::add_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return unodb::detail::impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }
};

static_assert(sizeof(inode_16) == 160);

class [[nodiscard]] inode_48 final
    : public unodb::detail::basic_inode_48<art_policy> {
 public:
  using basic_inode_48::basic_inode_48;

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return unodb::detail::impl_helpers::add_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return unodb::detail::impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }
};

#ifdef UNODB_DETAIL_AVX2
static_assert(sizeof(inode_48) == 672);
#else
static_assert(sizeof(inode_48) == 656);
#endif

class [[nodiscard]] inode_256 final
    : public unodb::detail::basic_inode_256<art_policy> {
 public:
  using basic_inode_256::basic_inode_256;

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return unodb::detail::impl_helpers::add_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return unodb::detail::impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }
};

static_assert(sizeof(inode_256) == 2064);

// Because we cannot dereference, load(), & take address of - it is a temporary
// by then
UNODB_DETAIL_DISABLE_MSVC_WARNING(26490)
inline auto *unwrap_fake_critical_section(
    unodb::in_fake_critical_section<unodb::detail::node_ptr> *ptr) noexcept {
  return reinterpret_cast<unodb::detail::node_ptr *>(ptr);
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

}  // namespace

namespace unodb::detail {

template <class INode>
detail::node_ptr *impl_helpers::add_or_choose_subtree(
    INode &inode, std::byte key_byte, art_key k, value_view v, db &db_instance,
    tree_depth depth, detail::node_ptr *node_in_parent) {
  auto *const child = unwrap_fake_critical_section(
      static_cast<INode &>(inode).find_child(key_byte).second);

  if (child != nullptr) return child;

  auto leaf = art_policy::make_db_leaf_ptr(k, v, db_instance);
  const auto children_count = inode.get_children_count();

  if constexpr (!std::is_same_v<INode, inode_256>) {
    if (UNODB_DETAIL_UNLIKELY(children_count == INode::capacity)) {
      auto larger_node{INode::larger_derived_type::create(
          db_instance, inode, std::move(leaf), depth)};
      *node_in_parent =
          node_ptr{larger_node.release(), INode::larger_derived_type::type};
      db_instance
          .template account_growing_inode<INode::larger_derived_type::type>();
      return child;
    }
  }
  inode.add_to_nonfull(std::move(leaf), depth, children_count);
  return child;
}

template <class INode>
std::optional<detail::node_ptr *> impl_helpers::remove_or_choose_subtree(
    INode &inode, std::byte key_byte, detail::art_key k, db &db_instance,
    detail::node_ptr *node_in_parent) {
  const auto [child_i, child_ptr]{inode.find_child(key_byte)};

  if (child_ptr == nullptr) return {};

  const auto child_ptr_val{child_ptr->load()};
  if (child_ptr_val.type() != node_type::LEAF)
    return unwrap_fake_critical_section(child_ptr);

  const auto *const leaf{child_ptr_val.template ptr<::leaf *>()};
  if (!leaf->matches(k)) return {};

  if (UNODB_DETAIL_UNLIKELY(inode.is_min_size())) {
    if constexpr (std::is_same_v<INode, inode_4>) {
      auto current_node{
          art_policy::make_db_inode_unique_ptr(&inode, db_instance)};
      *node_in_parent = current_node->leave_last_child(child_i, db_instance);
    } else {
      auto new_node{
          INode::smaller_derived_type::create(db_instance, inode, child_i)};
      *node_in_parent =
          node_ptr{new_node.release(), INode::smaller_derived_type::type};
    }
    db_instance.template account_shrinking_inode<INode::type>();
    return nullptr;
  }

  inode.remove(child_i, db_instance);
  return nullptr;
}

}  // namespace unodb::detail

namespace unodb {

db::~db() noexcept { delete_root_subtree(); }

template <class INode>
constexpr void db::increment_inode_count() noexcept {
  static_assert(inode_defs::is_inode<INode>());

  ++node_counts[as_i<INode::type>];
  increase_memory_use(sizeof(INode));
}

template <class INode>
constexpr void db::decrement_inode_count() noexcept {
  static_assert(inode_defs::is_inode<INode>());
  UNODB_DETAIL_ASSERT(node_counts[as_i<INode::type>] > 0);

  --node_counts[as_i<INode::type>];
  decrease_memory_use(sizeof(INode));
}

template <node_type NodeType>
constexpr void db::account_growing_inode() noexcept {
  static_assert(NodeType != node_type::LEAF);

  ++growing_inode_counts[internal_as_i<NodeType>];
  UNODB_DETAIL_ASSERT(growing_inode_counts[internal_as_i<NodeType>] >=
                      node_counts[as_i<NodeType>]);
}

template <node_type NodeType>
constexpr void db::account_shrinking_inode() noexcept {
  static_assert(NodeType != node_type::LEAF);

  ++shrinking_inode_counts[internal_as_i<NodeType>];
  UNODB_DETAIL_ASSERT(shrinking_inode_counts[internal_as_i<NodeType>] <=
                      growing_inode_counts[internal_as_i<NodeType>]);
}

db::get_result db::get(key search_key) const noexcept {
  if (UNODB_DETAIL_UNLIKELY(root == nullptr)) return {};

  auto node{root};
  const detail::art_key k{search_key};
  auto remaining_key{k};

  while (true) {
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      const auto *const leaf{node.ptr<::leaf *>()};
      if (leaf->matches(k)) return leaf->get_value_view();
      return {};
    }

    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);

    auto *const inode{node.ptr<::inode *>()};
    const auto &key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    if (key_prefix.get_shared_length(remaining_key) < key_prefix_length)
      return {};
    remaining_key.shift_right(key_prefix_length);
    const auto *const child{
        inode->find_child(node_type, remaining_key[0]).second};
    if (child == nullptr) return {};

    node = *child;
    remaining_key.shift_right(1);
  }
}

UNODB_DETAIL_DISABLE_MSVC_WARNING(26430)
bool db::insert(key insert_key, value_view v) {
  const auto k = detail::art_key{insert_key};

  if (UNODB_DETAIL_UNLIKELY(root == nullptr)) {
    auto leaf = art_policy::make_db_leaf_ptr(k, v, *this);
    root = detail::node_ptr{leaf.release(), node_type::LEAF};
    return true;
  }

  auto *node = &root;
  detail::tree_depth depth{};
  auto remaining_key{k};

  while (true) {
    const auto node_type = node->type();
    if (node_type == node_type::LEAF) {
      auto *const leaf{node->ptr<::leaf *>()};
      const auto existing_key{leaf->get_key()};
      if (UNODB_DETAIL_UNLIKELY(k == existing_key)) return false;

      auto new_leaf = art_policy::make_db_leaf_ptr(k, v, *this);
      auto new_node{inode_4::create(*this, existing_key, remaining_key, depth,
                                    leaf, std::move(new_leaf))};
      *node = detail::node_ptr{new_node.release(), node_type::I4};
      account_growing_inode<node_type::I4>();
      return true;
    }

    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);
    UNODB_DETAIL_ASSERT(depth < detail::art_key::size);

    auto *const inode{node->ptr<::inode *>()};
    const auto &key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    const auto shared_prefix_len{key_prefix.get_shared_length(remaining_key)};
    if (shared_prefix_len < key_prefix_length) {
      auto leaf = art_policy::make_db_leaf_ptr(k, v, *this);
      auto new_node = inode_4::create(*this, *node, shared_prefix_len, depth,
                                      std::move(leaf));
      *node = detail::node_ptr{new_node.release(), node_type::I4};
      account_growing_inode<node_type::I4>();
      ++key_prefix_splits;
      UNODB_DETAIL_ASSERT(growing_inode_counts[internal_as_i<node_type::I4>] >
                          key_prefix_splits);
      return true;
    }

    UNODB_DETAIL_ASSERT(shared_prefix_len == key_prefix_length);
    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    node = inode->add_or_choose_subtree<detail::node_ptr *>(
        node_type, remaining_key[0], k, v, *this, depth, node);

    if (node == nullptr) return true;

    ++depth;
    remaining_key.shift_right(1);
  }
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

bool db::remove(key remove_key) {
  const auto k = detail::art_key{remove_key};

  if (UNODB_DETAIL_UNLIKELY(root == nullptr)) return false;

  if (root.type() == node_type::LEAF) {
    auto *const root_leaf{root.ptr<leaf *>()};
    if (root_leaf->matches(k)) {
      const auto r{art_policy::reclaim_leaf_on_scope_exit(root_leaf, *this)};
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
    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);
    UNODB_DETAIL_ASSERT(depth < detail::art_key::size);

    auto *const inode{node->ptr<::inode *>()};
    const auto &key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    const auto shared_prefix_len{key_prefix.get_shared_length(remaining_key)};
    if (shared_prefix_len < key_prefix_length) return false;

    UNODB_DETAIL_ASSERT(shared_prefix_len == key_prefix_length);
    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    const auto remove_result{
        inode->remove_or_choose_subtree<std::optional<detail::node_ptr *>>(
            node_type, remaining_key[0], k, *this, node)};
    if (UNODB_DETAIL_UNLIKELY(!remove_result)) return false;

    auto *const child_ptr{*remove_result};
    if (child_ptr == nullptr) return true;

    node = child_ptr;
    ++depth;
    remaining_key.shift_right(1);
  }
}

void db::delete_root_subtree() noexcept {
  if (root != nullptr) art_policy::delete_subtree(root, *this);

  // It is possible to reset the counter to zero instead of decrementing it for
  // each leaf, but not sure the savings will be significant.
  UNODB_DETAIL_ASSERT(node_counts[as_i<node_type::LEAF>] == 0);
}

void db::clear() noexcept {
  delete_root_subtree();

  root = nullptr;
  current_memory_use = 0;
  node_counts[as_i<node_type::I4>] = 0;
  node_counts[as_i<node_type::I16>] = 0;
  node_counts[as_i<node_type::I48>] = 0;
  node_counts[as_i<node_type::I256>] = 0;
}

void db::dump(std::ostream &os) const {
  os << "db dump, current memory use = " << get_current_memory_use() << '\n';
  art_policy::dump_node(os, root);
}

}  // namespace unodb
