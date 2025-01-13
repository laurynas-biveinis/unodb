// Copyright 2019-2025 UnoDB contributors

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

// IWYU pragma: no_include <__ostream/basic_ostream.h>

#include "art.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>  // IWYU pragma: keep
#include <optional>
#include <type_traits>
#include <utility>

#include "art_common.hpp"
#include "art_internal.hpp"
#include "art_internal_impl.hpp"
#include "assert.hpp"
#include "in_fake_critical_section.hpp"
#include "node_type.hpp"

namespace {

using leaf = unodb::detail::basic_leaf<unodb::detail::node_header>;

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

class [[nodiscard]] inode_4 final
    : public unodb::detail::basic_inode_4<unodb::detail::art_policy> {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
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
    : public unodb::detail::basic_inode_16<unodb::detail::art_policy> {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
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
    : public unodb::detail::basic_inode_48<unodb::detail::art_policy> {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
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
    : public unodb::detail::basic_inode_256<unodb::detail::art_policy> {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
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

}  // namespace unodb::detail

namespace {

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
  auto *const child =
      unwrap_fake_critical_section(inode.find_child(key_byte).second);

  if (child != nullptr) return child;

  auto aleaf = art_policy::make_db_leaf_ptr(k, v, db_instance);
  const auto children_count = inode.get_children_count();

  if constexpr (!std::is_same_v<INode, inode_256>) {
    if (UNODB_DETAIL_UNLIKELY(children_count == INode::capacity)) {
      auto larger_node{INode::larger_derived_type::create(
          db_instance, inode, std::move(aleaf), depth)};
      *node_in_parent =
          node_ptr{larger_node.release(), INode::larger_derived_type::type};
#ifdef UNODB_DETAIL_WITH_STATS
      db_instance
          .template account_growing_inode<INode::larger_derived_type::type>();
#endif  // UNODB_DETAIL_WITH_STATS
      return child;
    }
  }
  inode.add_to_nonfull(std::move(aleaf), depth, children_count);
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

  const auto *const aleaf{child_ptr_val.template ptr<::leaf *>()};
  if (!aleaf->matches(k)) return {};

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
#ifdef UNODB_DETAIL_WITH_STATS
    db_instance.template account_shrinking_inode<INode::type>();
#endif  // UNODB_DETAIL_WITH_STATS
    return nullptr;
  }

  inode.remove(child_i, db_instance);
  return nullptr;
}

}  // namespace unodb::detail

namespace unodb {

db::~db() noexcept { delete_root_subtree(); }

#ifdef UNODB_DETAIL_WITH_STATS

template <class INode>
constexpr void db::increment_inode_count() noexcept {
  static_assert(unodb::detail::inode_defs::is_inode<INode>());

  ++node_counts[as_i<INode::type>];
  increase_memory_use(sizeof(INode));
}

template <class INode>
constexpr void db::decrement_inode_count() noexcept {
  static_assert(unodb::detail::inode_defs::is_inode<INode>());
  UNODB_DETAIL_ASSERT(node_counts[as_i<INode::type>] > 0);

  --node_counts[as_i<INode::type>];
  decrease_memory_use(sizeof(INode));
}

template <node_type NodeType>
constexpr void db::account_growing_inode() noexcept {
  static_assert(NodeType != node_type::LEAF);

  // NOLINTNEXTLINE(google-readability-casting)
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

#endif  // UNODB_DETAIL_WITH_STATS

db::get_result db::get0(const detail::art_key k) const noexcept {
  if (UNODB_DETAIL_UNLIKELY(root == nullptr)) return {};

  auto node{root};
  // const detail::art_key k{search_key};
  auto remaining_key{k};

  while (true) {
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      const auto *const leaf{node.ptr<::leaf *>()};
      if (leaf->matches(k)) return leaf->get_value_view();
      return {};
    }

    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);

    auto *const inode{node.ptr<detail::inode *>()};
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
bool db::insert0(const detail::art_key k, value_view v) {
  // const auto k = detail::art_key{insert_key};

  if (UNODB_DETAIL_UNLIKELY(root == nullptr)) {
    auto leaf = unodb::detail::art_policy::make_db_leaf_ptr(k, v, *this);
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
      if (UNODB_DETAIL_UNLIKELY(k.cmp(existing_key) == 0)) return false;

      auto new_leaf = unodb::detail::art_policy::make_db_leaf_ptr(k, v, *this);
      auto new_node{detail::inode_4::create(*this, existing_key, remaining_key,
                                            depth, leaf, std::move(new_leaf))};
      *node = detail::node_ptr{new_node.release(), node_type::I4};
#ifdef UNODB_DETAIL_WITH_STATS
      account_growing_inode<node_type::I4>();
#endif  // UNODB_DETAIL_WITH_STATS
      return true;
    }

    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);
    UNODB_DETAIL_ASSERT(depth < detail::art_key::size);

    auto *const inode{node->ptr<detail::inode *>()};
    const auto &key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    const auto shared_prefix_len{key_prefix.get_shared_length(remaining_key)};
    if (shared_prefix_len < key_prefix_length) {
      auto leaf = unodb::detail::art_policy::make_db_leaf_ptr(k, v, *this);
      auto new_node = detail::inode_4::create(*this, *node, shared_prefix_len,
                                              depth, std::move(leaf));
      *node = detail::node_ptr{new_node.release(), node_type::I4};
#ifdef UNODB_DETAIL_WITH_STATS
      account_growing_inode<node_type::I4>();
      ++key_prefix_splits;
      UNODB_DETAIL_ASSERT(growing_inode_counts[internal_as_i<node_type::I4>] >
                          key_prefix_splits);
#endif  // UNODB_DETAIL_WITH_STATS
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

bool db::remove0(const detail::art_key k) {
  // const auto k = detail::art_key{remove_key};

  if (UNODB_DETAIL_UNLIKELY(root == nullptr)) return false;

  if (root.type() == node_type::LEAF) {
    auto *const root_leaf{root.ptr<leaf *>()};
    if (root_leaf->matches(k)) {
      const auto r{
          detail::art_policy::reclaim_leaf_on_scope_exit(root_leaf, *this)};
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

    auto *const inode{node->ptr<detail::inode *>()};
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
  if (root != nullptr) detail::art_policy::delete_subtree(root, *this);

#ifdef UNODB_DETAIL_WITH_STATS
  // It is possible to reset the counter to zero instead of decrementing it for
  // each leaf, but not sure the savings will be significant.
  UNODB_DETAIL_ASSERT(node_counts[as_i<node_type::LEAF>] == 0);
#endif  // UNODB_DETAIL_WITH_STATS
}

void db::clear() noexcept {
  delete_root_subtree();

  root = nullptr;
#ifdef UNODB_DETAIL_WITH_STATS
  current_memory_use = 0;
  node_counts[as_i<node_type::I4>] = 0;
  node_counts[as_i<node_type::I16>] = 0;
  node_counts[as_i<node_type::I48>] = 0;
  node_counts[as_i<node_type::I256>] = 0;
#endif  // UNODB_DETAIL_WITH_STATS
}

void db::dump(std::ostream &os) const {
#ifdef UNODB_DETAIL_WITH_STATS
  os << "db dump, current memory use = " << get_current_memory_use() << '\n';
#else
  os << "db dump\n";
#endif  // UNODB_DETAIL_WITH_STATS
  detail::art_policy::dump_node(os, root);
}

// LCOV_EXCL_START
void db::dump() const { dump(std::cerr); }
// LCOV_EXCL_STOP

///
/// unodb::db::iterator
///

// LCOV_EXCL_START
void db::iterator::dump(std::ostream &os) const {
  if (empty()) {
    os << "iter::stack:: empty\n";
    return;
  }
  // Create a new stack and copy everything there.  Using the new
  // stack, print out the stack in top-bottom order.  This avoids
  // modifications to the existing stack for the iterator.
  auto tmp = stack_;
  auto level = tmp.size() - 1;
  while (!tmp.empty()) {
    const auto &e = tmp.top();
    const auto &np = e.node;
    os << "iter::stack:: level = " << level << ", key_byte=0x" << std::hex
       << std::setfill('0') << std::setw(2)
       << static_cast<std::uint64_t>(e.key_byte) << std::dec
       << ", child_index=0x" << std::hex << std::setfill('0') << std::setw(2)
       << static_cast<std::uint64_t>(e.child_index) << std::dec << ", ";
    detail::art_policy::dump_node(os, np, false /*recursive*/);
    if (np.type() != node_type::LEAF) os << '\n';
    tmp.pop();
    level--;
  }
}

void db::iterator::dump() const { dump(std::cerr); }
// LCOV_EXCL_STOP

// Traverse to the left-most leaf. The stack is cleared first and then
// re-populated as we step down along the path to the left-most leaf.
// If the tree is empty, then the result is the same as end().
db::iterator &db::iterator::first() {
  invalidate();  // clear the stack
  if (UNODB_DETAIL_UNLIKELY(db_.root == nullptr)) return *this;  // empty tree.
  auto node{db_.root};
  return left_most_traversal(node);
}

// Traverse to the right-most leaf. The stack is cleared first and then
// re-populated as we step down along the path to the right-most leaf.
// If the tree is empty, then the result is the same as end().
db::iterator &db::iterator::last() {
  invalidate();  // clear the stack
  if (UNODB_DETAIL_UNLIKELY(db_.root == nullptr)) return *this;  // empty tree.
  auto node{db_.root};
  return right_most_traversal(node);
}

// Position the iterator on the next leaf in the index.
db::iterator &db::iterator::next() {
  while (!empty()) {
    auto e = top();
    auto node{e.node};
    UNODB_DETAIL_ASSERT(node != nullptr);
    auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      pop();     // pop off the leaf
      continue;  // falls through loop if just a root leaf since stack now
                 // empty.
    }
    auto *inode{node.ptr<detail::inode *>()};
    auto nxt = inode->next(node_type,
                           e.child_index);  // next child of that parent.
    if (!nxt) {
      pop();     // Nothing more for that inode.
      continue;  // We will look for the right sibling of the parent inode.
    }
    // Fix up stack for new parent node state and left-most descent.
    UNODB_DETAIL_ASSERT(nxt.has_value());  // value exists for std::optional
    auto e2 = nxt.value();
    pop();
    push(e2);
    auto child = inode->get_child(node_type, e2.child_index);  // descend
    return left_most_traversal(child);
  }
  return *this;  // stack is empty, so iterator == end().
}

// Position the iterator on the prior leaf in the index.
db::iterator &db::iterator::prior() {
  while (!empty()) {
    auto e = top();
    auto node{e.node};
    UNODB_DETAIL_ASSERT(node != nullptr);
    auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      pop();     // pop off the leaf
      continue;  // falls through loop if just a root leaf since stack now
                 // empty.
    }
    auto *inode{node.ptr<detail::inode *>()};
    auto nxt = inode->prior(node_type, e.child_index);  // parent's prev child
    if (!nxt) {
      pop();     // Nothing more for that inode.
      continue;  // We will look for the left sibling of the parent inode.
    }
    // Fix up stack for new parent node state and right-most descent.
    UNODB_DETAIL_ASSERT(nxt.has_value());  // value exists for std::optional
    auto e2 = nxt.value();
    pop();
    push(e2);
    auto child = inode->get_child(node_type, e2.child_index);  // descend
    return right_most_traversal(child);
  }
  return *this;  // stack is empty, so iterator == end().
}

// Push the given node onto the stack and traverse from the caller's
// node to the left-most leaf under that node, pushing nodes onto the
// stack as they are visited.
inline db::iterator &db::iterator::left_most_traversal(detail::node_ptr node) {
  while (true) {
    UNODB_DETAIL_ASSERT(node != nullptr);
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      push_leaf(node);
      return *this;  // done
    }
    // recursive descent.
    auto *const inode{node.ptr<detail::inode *>()};
    auto e = inode->begin(node_type);  // first child of current internal node
    push(e);                           // push the entry on the stack.
    node = inode->get_child(node_type, e.child_index);  // get the child
  }
  UNODB_DETAIL_CANNOT_HAPPEN();
}

// Push the given node onto the stack and traverse from the caller's
// node to the right-most leaf under that node, pushing nodes onto the
// stack as they are visited.
inline db::iterator &db::iterator::right_most_traversal(detail::node_ptr node) {
  while (true) {
    UNODB_DETAIL_ASSERT(node != nullptr);
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      push_leaf(node);
      return *this;  // done
    }
    // recursive descent.
    auto *const inode{node.ptr<detail::inode *>()};
    auto e = inode->last(node_type);  // first child of current internal node
    push(e);                          // push the entry on the stack.
    node = inode->get_child(node_type, e.child_index);  // get the child
  }
  UNODB_DETAIL_CANNOT_HAPPEN();
}

// Note: The basic seek() logic is similar to ::get() as long as the
// search_key exists in the data.  However, the iterator is positioned
// instead of returning the value for the key.  Life gets a lot more
// complicated when the search_key is not in the data and we have to
// consider the cases for both forward traversal and reverse traversal
// from a key that is not in the data.
db::iterator &db::iterator::seek(detail::art_key search_key, bool &match,
                                 bool fwd) {
  invalidate();   // invalidate the iterator (clear the stack).
  match = false;  // unless we wind up with an exact match.
  if (UNODB_DETAIL_UNLIKELY(db_.root == nullptr)) return *this;  // aka end()

  auto node{db_.root};
  const detail::art_key k = search_key;
  auto remaining_key{k};

  while (true) {
    const auto node_type = node.type();
    if (node_type == node_type::LEAF) {
      const auto *const leaf{node.ptr<detail::leaf *>()};
      push_leaf(node);
      const auto cmp_ = leaf->cmp(k);
      if (cmp_ == 0) {
        match = true;
        return *this;
      }
      if (fwd) {  // GTE semantics
        // if search_key < leaf, use the leaf, else next().
        return (cmp_ < 0) ? *this : next();
      }
      // LTE semantics: if search_key > leaf, use the leaf, else prior().
      return (cmp_ > 0) ? *this : prior();
    }
    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);
    auto *const inode{node.ptr<detail::inode *>()};   // some internal node.
    const auto &key_prefix{inode->get_key_prefix()};  // prefix for that node.
    const auto key_prefix_length{
        key_prefix.length()};  // length of that prefix.
    const auto shared_length = key_prefix.get_shared_length(
        remaining_key);  // #of prefix bytes matched.
    if (shared_length < key_prefix_length) {
      // We have visited an internal node whose prefix is longer than
      // the bytes in the key that we need to match.  To figure out
      // whether the search key would be located before or after the
      // current internal node, we need to compare the respective key
      // spans lexicographically.  Since we have [shared_length] bytes
      // in common, we know that the next byte will tell us the
      // relative ordering of the key vs the prefix. So now we compare
      // prefix and key and the first byte where they differ.
      const auto cmp_ = static_cast<int>(remaining_key[shared_length]) -
                        static_cast<int>(key_prefix[shared_length]);
      UNODB_DETAIL_ASSERT(cmp_ != 0);
      if (fwd) {
        if (cmp_ < 0) {
          // FWD and the search key is ordered before this node.  We
          // want the left-most leaf under the node.
          return left_most_traversal(node);
        }
        // FWD and the search key is ordered after this node.  Right
        // most descent and then next().
        return right_most_traversal(node).next();
      }
      // reverse traversal
      if (cmp_ < 0) {
        // REV and the search key is ordered before this node.  We
        // want the preceeding key.
        return left_most_traversal(node).prior();
      }
      // REV and the search key is ordered after this node.
      return right_most_traversal(node);
    }
    remaining_key.shift_right(key_prefix_length);
    auto res = inode->find_child(node_type, remaining_key[0]);
    if (res.second == nullptr) {
      // We are on a key byte during the descent that is not mapped by
      // the current node.  Where we go next depends on whether we are
      // doing forward or reverse traversal.
      if (fwd) {
        // FWD: Take the next child_index that is mapped in the data
        // and then do a left-most descent to land on the key that is
        // the immediate successor of the desired key in the data.
        //
        // Note: We are probing with a key byte which does not appear
        // in our list of keys (this was verified above) so this will
        // always be the index the first entry whose key byte is
        // greater-than the probe value and [false] if there is no
        // such entry.
        //
        // Note: [node] has not been pushed onto the stack yet!
        auto nxt = inode->gte_key_byte(node_type, remaining_key[0]);
        if (!nxt) {
          // Pop entries off the stack until we find one with a
          // right-sibling of the path we took to this node and then
          // do a left-most descent under that right-sibling. If there
          // is no such parent, we will wind up with an empty stack
          // (aka the end() iterator) and return that state.
          if (!empty()) pop();
          while (!empty()) {
            const auto centry = top();
            const auto cnode{centry.node};  // possible parent from the stack
            auto *const icnode{cnode.ptr<detail::inode *>()};
            const auto cnxt = icnode->next(
                cnode.type(), centry.child_index);  // right-sibling.
            if (cnxt) {
              auto nchild = icnode->get_child(cnode.type(), centry.child_index);
              return left_most_traversal(nchild);
            }
            pop();
          }
          return *this;  // stack is empty (aka end()).
        }
        auto tmp = nxt.value();  // unwrap.
        const auto child_index = tmp.child_index;
        const auto child = inode->get_child(node_type, child_index);
        push(node, tmp.key_byte, child_index);  // the path we took
        return left_most_traversal(child);      // left most traversal
      }
      // REV: Take the prior child_index that is mapped and then do
      // a right-most descent to land on the key that is the
      // immediate precessor of the desired key in the data.
      auto nxt = inode->lte_key_byte(node_type, remaining_key[0]);
      if (!nxt) {
        // Pop off the current entry until we find one with a
        // left-sibling and then do a right-most descent under that
        // left-sibling.  In the extreme case there is no such
        // previous entry and we will wind up with an empty stack.
        if (!empty()) pop();
        while (!empty()) {
          const auto centry = top();
          const auto cnode{centry.node};  // possible parent from stack
          auto *const icnode{cnode.ptr<detail::inode *>()};
          const auto cnxt =
              icnode->prior(cnode.type(), centry.child_index);  // left-sibling.
          if (cnxt) {
            auto nchild = icnode->get_child(cnode.type(), centry.child_index);
            return right_most_traversal(nchild);
          }
          pop();
        }
        return *this;  // stack is empty (aka end()).
      }
      auto tmp = nxt.value();  // unwrap.
      const auto child_index{tmp.child_index};
      const auto child = inode->get_child(node_type, child_index);
      push(node, tmp.key_byte, child_index);  // the path we took
      return right_most_traversal(child);     // right most traversal
    }
    // Simple case. There is a child for the current key byte.
    const auto child_index{res.first};
    const auto *const child{res.second};
    push(node, remaining_key[0], child_index);
    node = *child;
    remaining_key.shift_right(1);
  }  // while ( true )
  UNODB_DETAIL_CANNOT_HAPPEN();
}

}  // namespace unodb
