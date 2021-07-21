// Copyright 2019-2021 Laurynas Biveinis

#include "art_internal.hpp"
#include "global.hpp"

#include "olc_art.hpp"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "art_internal_impl.hpp"
#include "heap.hpp"
#include "node_type.hpp"
#include "optimistic_lock.hpp"
#include "qsbr.hpp"

namespace unodb::detail {

struct olc_node_header {
  [[nodiscard]] constexpr optimistic_lock &lock() const noexcept {
    return m_lock;
  }

 private:
  mutable optimistic_lock m_lock;

  friend void olc_node_header_static_asserts();
};

template <class Header, class Db>
class db_leaf_qsbr_deleter {
 public:
  static_assert(std::is_trivially_destructible_v<basic_leaf<Header>>);

  constexpr explicit db_leaf_qsbr_deleter(Db &db_) noexcept
      : db_instance{db_} {}

  void operator()(raw_leaf_ptr to_delete) const noexcept {
    const auto leaf_size = basic_leaf<Header>::size(to_delete);

    qsbr::instance().on_next_epoch_pool_deallocate(get_leaf_node_pool(),
                                                   to_delete, leaf_size,
                                                   alignment_for_new<Header>());

    db_instance.decrement_leaf_count(leaf_size);
  }

 private:
  Db &db_instance;
};

}  // namespace unodb::detail

namespace {

template <class INode>
struct inode_pool_getter {
  [[nodiscard]] static inline auto &get() {
    static unodb::detail::pmr_synchronized_pool_resource inode_pool{
        unodb::detail::get_inode_pool_options<INode>()};
    return inode_pool;
  }

  inode_pool_getter() = delete;
};

template <class INode>
using db_inode_qsbr_deleter_parent = unodb::detail::basic_db_inode_deleter<
    INode, unodb::olc_db, unodb::detail::olc_inode_defs, inode_pool_getter>;

}  // namespace

namespace unodb::detail {

template <class INode>
class db_inode_qsbr_deleter : public db_inode_qsbr_deleter_parent<INode> {
 public:
  using db_inode_qsbr_deleter_parent<INode>::db_inode_qsbr_deleter_parent;

  void operator()(INode *inode_ptr) noexcept {
    static_assert(std::is_trivially_destructible_v<INode>);

    qsbr::instance().on_next_epoch_pool_deallocate(
        inode_pool_getter<INode>::get(), inode_ptr, sizeof(INode),
        alignment_for_new<INode>());

    this->get_db().template decrement_inode_count<INode>();
  }
};

}  // namespace unodb::detail

namespace {

template <class INode>
[[nodiscard]] auto make_db_inode_reclaimable_ptr(unodb::olc_db &db_instance,
                                                 INode *inode_ptr) {
  return std::unique_ptr<INode, unodb::detail::db_inode_qsbr_deleter<INode>>{
      inode_ptr, unodb::detail::db_inode_qsbr_deleter<INode>{db_instance}};
}

using olc_art_policy = unodb::detail::basic_art_policy<
    unodb::olc_db, unodb::in_critical_section, unodb::detail::olc_node_ptr,
    unodb::detail::db_inode_qsbr_deleter, unodb::detail::db_leaf_qsbr_deleter,
    inode_pool_getter>;

using olc_db_leaf_unique_ptr = olc_art_policy::db_leaf_unique_ptr;

using leaf = olc_art_policy::leaf_type;

using olc_inode_base = unodb::detail::basic_inode_impl<olc_art_policy>;

[[nodiscard]] auto &node_ptr_lock(
    const unodb::detail::olc_node_ptr &node) noexcept {
  return node.as_header()->lock();
}

#ifndef NDEBUG

[[nodiscard]] auto &node_ptr_lock(unodb::detail::raw_leaf_ptr node) noexcept {
  return reinterpret_cast<unodb::detail::olc_node_header *>(
             __builtin_assume_aligned(node,
                                      alignof(unodb::detail::olc_node_header)))
      ->lock();
}

#endif

template <class INode>
[[nodiscard]] constexpr auto &lock(const INode &inode) noexcept {
  return inode.lock();
}

template <class T>
std::remove_reference_t<T> &&obsolete_and_move(
    T &&t, unodb::optimistic_lock::write_guard &&guard) noexcept {
  assert(guard.guards(lock(*t)));

  // My first attempt was to pass guard by value and let it destruct at the end
  // of this scope, but due to copy elision (?) the destructor got called way
  // too late, after the owner node was destructed.
  guard.unlock_and_obsolete();

  return static_cast<std::remove_reference_t<T> &&>(t);
}

inline auto obsolete_child_by_index(
    std::uint8_t child, unodb::optimistic_lock::write_guard &&guard) noexcept {
  guard.unlock_and_obsolete();

  return child;
}

}  // namespace

namespace unodb::detail {

// Wrap olc_inode_add in a struct so that the latter and not the former could be
// declared as friend of olc_db, avoiding the need to forward declare the likes
// of olc_db_leaf_unique_ptr.
struct olc_impl_helpers {
  // GCC 10 diagnoses parameters that are present only in uninstantiated if
  // constexpr branch, such as node_in_parent for olc_inode_256.
  UNODB_DETAIL_DISABLE_GCC_10_WARNING("-Wunused-parameter")

  template <class INode>
  [[nodiscard]] static std::optional<in_critical_section<olc_node_ptr> *>
  add_or_choose_subtree(
      INode &inode, std::byte key_byte, art_key k, value_view v,
      olc_db &db_instance, tree_depth depth,
      optimistic_lock::read_critical_section &node_critical_section,
      in_critical_section<olc_node_ptr> *node_in_parent,
      optimistic_lock::read_critical_section &parent_critical_section);

  UNODB_DETAIL_RESTORE_GCC_10_WARNINGS()

  template <class INode>
  [[nodiscard]] static std::optional<bool> remove_or_choose_subtree(
      INode &inode, std::byte key_byte, detail::art_key k, olc_db &db_instance,
      optimistic_lock::read_critical_section &parent_critical_section,
      optimistic_lock::read_critical_section &node_critical_section,
      in_critical_section<olc_node_ptr> *node_in_parent,
      in_critical_section<olc_node_ptr> **child_in_parent,
      optimistic_lock::read_critical_section *child_critical_section,
      node_type *child_type, olc_node_ptr *child);

  olc_impl_helpers() = delete;
};

class olc_inode : public olc_inode_base {};

class olc_inode_4 final : public basic_inode_4<olc_art_policy> {
  using parent_class = basic_inode_4<olc_art_policy>;

 public:
  constexpr olc_inode_4(art_key k1, art_key shifted_k2, tree_depth depth,
                        raw_leaf_ptr child1,
                        olc_db_leaf_unique_ptr &&child2) noexcept
      : parent_class{k1, shifted_k2, depth, child1, std::move(child2)} {
    assert(node_ptr_lock(child1).is_write_locked());
  }

  constexpr olc_inode_4(olc_node_ptr source_node, unsigned len,
                        tree_depth depth,
                        olc_db_leaf_unique_ptr &&child1) noexcept
      : parent_class{source_node, len, depth, std::move(child1)} {
    assert(node_ptr_lock(source_node).is_write_locked());
  }

  olc_inode_4(db_inode16_reclaimable_ptr &&source_node,
              optimistic_lock::write_guard &&source_node_guard,
              std::uint8_t child_to_delete,
              optimistic_lock::write_guard &&child_guard) noexcept;

  [[nodiscard]] static db_inode4_unique_ptr create(
      db_inode16_reclaimable_ptr &&source_node,
      optimistic_lock::write_guard &&source_node_guard,
      std::uint8_t child_to_delete, optimistic_lock::write_guard &&child_guard);

  // Create a new node with two given child leaves
  [[nodiscard]] static auto create(art_key k1, art_key shifted_k2,
                                   tree_depth depth, raw_leaf_ptr child1,
                                   olc_db_leaf_unique_ptr &&child2) {
    assert(node_ptr_lock(child1).is_write_locked());

    return basic_inode_4::create(k1, shifted_k2, depth, child1,
                                 std::move(child2));
  }

  // Create a new node, split the key prefix of an existing node, and make the
  // new node contain that existing node and a given new node which caused
  // this key prefix split.
  [[nodiscard]] static auto create(olc_node_ptr source_node, unsigned len,
                                   tree_depth depth,
                                   olc_db_leaf_unique_ptr &&child1) {
    assert(node_ptr_lock(source_node).is_write_locked());

    return basic_inode_4::create(source_node, len, depth, std::move(child1));
  }

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return olc_impl_helpers::add_or_choose_subtree(*this,
                                                   std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return olc_impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  void remove(std::uint8_t child_index, olc_db &db_instance) noexcept {
    assert(::lock(*this).is_write_locked());

    basic_inode_4::remove(child_index, db_instance);
  }

  void add_two_to_empty(std::byte key1, olc_node_ptr child1, std::byte key2,
                        olc_db_leaf_unique_ptr &&child2) noexcept {
    assert(node_ptr_lock(child1).is_write_locked());

    basic_inode_4::add_two_to_empty(key1, child1, key2, std::move(child2));
  }

  auto leave_last_child(std::uint8_t child_to_delete,
                        olc_db &db_instance) noexcept {
    assert(::lock(*this).is_obsoleted_by_this_thread());
    assert(node_ptr_lock(children[child_to_delete].load())
               .is_obsoleted_by_this_thread());

    return basic_inode_4::leave_last_child(child_to_delete, db_instance);
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    os << ", ";
    ::lock(*this).dump(os);
    basic_inode_4::dump(os);
  }
};

// 48 == sizeof(inode_4)
#ifdef NDEBUG
static_assert(sizeof(olc_inode_4) == 48 + 8);
#else
static_assert(sizeof(olc_inode_4) == 48 + 24);
#endif

class olc_inode_16 final : public basic_inode_16<olc_art_policy> {
  using parent_class = basic_inode_16<olc_art_policy>;

 public:
  olc_inode_16(db_inode4_reclaimable_ptr &&source_node,
               optimistic_lock::write_guard &&source_node_guard,
               olc_db_leaf_unique_ptr &&child, tree_depth depth) noexcept
      : parent_class{
            obsolete_and_move(source_node, std::move(source_node_guard)),
            std::move(child), depth} {
    assert(!source_node_guard.active());
  }

  olc_inode_16(db_inode48_reclaimable_ptr &&source_node,
               optimistic_lock::write_guard &&source_node_guard,
               std::uint8_t child_to_delete,
               optimistic_lock::write_guard &&child_guard) noexcept;

  [[nodiscard]] static auto create(
      db_inode4_reclaimable_ptr &&source_node,
      optimistic_lock::write_guard &&source_node_guard,
      olc_db_leaf_unique_ptr &&child, tree_depth depth) {
    assert(source_node_guard.guards(::lock(*source_node)));

    return olc_art_policy::make_db_inode_unique_ptr<olc_inode_16>(
        child.get_deleter().get_db(), std::move(source_node),
        std::move(source_node_guard), std::move(child), depth);
  }

  [[nodiscard]] static db_inode16_unique_ptr create(
      db_inode48_reclaimable_ptr &&source_node,
      optimistic_lock::write_guard &&source_node_guard,
      std::uint8_t child_to_delete, optimistic_lock::write_guard &&child_guard);

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return olc_impl_helpers::add_or_choose_subtree(*this,
                                                   std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return olc_impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  void remove(std::uint8_t child_index, olc_db &db_instance) noexcept {
    assert(::lock(*this).is_write_locked());

    basic_inode_16::remove(child_index, db_instance);
  }

  [[nodiscard]] find_result find_child(std::byte key_byte) noexcept {
#ifdef UNODB_DETAIL_THREAD_SANITIZER
    const auto children_count_ = this->children_count.load();
    for (unsigned i = 0; i < children_count_; ++i)
      if (keys.byte_array[i] == key_byte)
        return std::make_pair(i, &children[i]);
    return std::make_pair(0xFF, nullptr);
#elif defined(__x86_64)
    return basic_inode_16::find_child(key_byte);
#else
#error Needs porting
#endif
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    os << ", ";
    ::lock(*this).dump(os);
    basic_inode_16::dump(os);
  }
};

// 160 == sizeof(inode_16)
#ifdef NDEBUG
static_assert(sizeof(olc_inode_16) == 160 + 16);
#else
static_assert(sizeof(olc_inode_16) == 160 + 32);
#endif

olc_inode_4::olc_inode_4(db_inode16_reclaimable_ptr &&source_node,
                         optimistic_lock::write_guard &&source_node_guard,
                         std::uint8_t child_to_delete,
                         optimistic_lock::write_guard &&child_guard) noexcept
    : parent_class{
          obsolete_and_move(source_node, std::move(source_node_guard)),
          obsolete_child_by_index(child_to_delete, std::move(child_guard))} {
  assert(!source_node_guard.active());
  assert(!child_guard.active());
}

[[nodiscard]] inline olc_inode_4::db_inode4_unique_ptr olc_inode_4::create(
    db_inode16_reclaimable_ptr &&source_node,
    optimistic_lock::write_guard &&source_node_guard,
    std::uint8_t child_to_delete, optimistic_lock::write_guard &&child_guard) {
  assert(source_node_guard.guards(::lock(*source_node)));
  assert(child_guard.active());

  return olc_art_policy::make_db_inode_unique_ptr<olc_inode_4>(
      source_node.get_deleter().get_db(), std::move(source_node),
      std::move(source_node_guard), child_to_delete, std::move(child_guard));
}

class olc_inode_48 final : public basic_inode_48<olc_art_policy> {
  using parent_class = basic_inode_48<olc_art_policy>;

 public:
  olc_inode_48(db_inode16_reclaimable_ptr &&source_node,
               optimistic_lock::write_guard &&source_node_guard,
               olc_db_leaf_unique_ptr &&child, tree_depth depth) noexcept
      : parent_class{
            obsolete_and_move(source_node, std::move(source_node_guard)),
            std::move(child), depth} {
    assert(!source_node_guard.active());
  }

  [[nodiscard]] static auto create(
      db_inode16_reclaimable_ptr &&source_node,
      optimistic_lock::write_guard &&source_node_guard,
      olc_db_leaf_unique_ptr &&child, tree_depth depth) {
    assert(source_node_guard.guards(::lock(*source_node)));

    return olc_art_policy::make_db_inode_unique_ptr<olc_inode_48>(
        child.get_deleter().get_db(), std::move(source_node),
        std::move(source_node_guard), std::move(child), depth);
  }

  olc_inode_48(db_inode256_reclaimable_ptr &&source_node,
               optimistic_lock::write_guard &&source_node_guard,
               std::uint8_t child_to_delete,
               optimistic_lock::write_guard &&child_guard) noexcept;

  [[nodiscard]] static db_inode48_unique_ptr create(
      db_inode256_reclaimable_ptr &&source_node,
      optimistic_lock::write_guard &&source_node_guard,
      std::uint8_t child_to_delete, optimistic_lock::write_guard &&child_guard);

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return olc_impl_helpers::add_or_choose_subtree(*this,
                                                   std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return olc_impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  void remove(std::uint8_t child_index, olc_db &db_instance) noexcept {
    assert(::lock(*this).is_write_locked());

    basic_inode_48::remove(child_index, db_instance);
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    os << ", ";
    ::lock(*this).dump(os);
    basic_inode_48::dump(os);
  }
};

// 656 == sizeof(inode_48)
#ifdef NDEBUG
static_assert(sizeof(olc_inode_48) == 656 + 16);
#else
static_assert(sizeof(olc_inode_48) == 656 + 32);
#endif

[[nodiscard]] inline olc_inode_16::db_inode16_unique_ptr olc_inode_16::create(
    db_inode48_reclaimable_ptr &&source_node,
    optimistic_lock::write_guard &&source_node_guard,
    std::uint8_t child_to_delete, optimistic_lock::write_guard &&child_guard) {
  assert(source_node_guard.guards(::lock(*source_node)));
  assert(child_guard.active());

  return olc_art_policy::make_db_inode_unique_ptr<olc_inode_16>(
      source_node.get_deleter().get_db(), std::move(source_node),
      std::move(source_node_guard), child_to_delete, std::move(child_guard));
}

olc_inode_16::olc_inode_16(db_inode48_reclaimable_ptr &&source_node,
                           optimistic_lock::write_guard &&source_node_guard,
                           std::uint8_t child_to_delete,
                           optimistic_lock::write_guard &&child_guard) noexcept
    : parent_class{
          obsolete_and_move(source_node, std::move(source_node_guard)),
          obsolete_child_by_index(child_to_delete, std::move(child_guard))} {
  assert(!source_node_guard.active());
  assert(!child_guard.active());
}

class olc_inode_256 final : public basic_inode_256<olc_art_policy> {
  using parent_class = basic_inode_256<olc_art_policy>;

 public:
  olc_inode_256(db_inode48_reclaimable_ptr &&source_node,
                optimistic_lock::write_guard &&source_node_guard,
                olc_db_leaf_unique_ptr &&child, tree_depth depth) noexcept
      : parent_class{
            obsolete_and_move(source_node, std::move(source_node_guard)),
            std::move(child), depth} {
    assert(!source_node_guard.active());
  }

  [[nodiscard]] static auto create(
      db_inode48_reclaimable_ptr &&source_node,
      optimistic_lock::write_guard &&source_node_guard,
      olc_db_leaf_unique_ptr &&child, tree_depth depth) {
    assert(source_node_guard.guards(::lock(*source_node)));

    return olc_art_policy::make_db_inode_unique_ptr<olc_inode_256>(
        child.get_deleter().get_db(), std::move(source_node),
        std::move(source_node_guard), std::move(child), depth);
  }

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return olc_impl_helpers::add_or_choose_subtree(*this,
                                                   std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return olc_impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  void remove(std::uint8_t child_index, olc_db &db_instance) noexcept {
    assert(::lock(*this).is_write_locked());

    basic_inode_256::remove(child_index, db_instance);
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    os << ", ";
    ::lock(*this).dump(os);
    basic_inode_256::dump(os);
  }
};

// 2064 == sizeof(inode_256)
#ifdef NDEBUG
static_assert(sizeof(olc_inode_256) == 2064 + 8);
#else
static_assert(sizeof(olc_inode_256) == 2064 + 24);
#endif

[[nodiscard]] inline olc_inode_48::db_inode48_unique_ptr olc_inode_48::create(
    db_inode256_reclaimable_ptr &&source_node,
    optimistic_lock::write_guard &&source_node_guard,
    std::uint8_t child_to_delete, optimistic_lock::write_guard &&child_guard) {
  assert(source_node_guard.guards(::lock(*source_node)));
  assert(child_guard.active());

  return olc_art_policy::make_db_inode_unique_ptr<olc_inode_48>(
      source_node.get_deleter().get_db(), std::move(source_node),
      std::move(source_node_guard), child_to_delete, std::move(child_guard));
}

olc_inode_48::olc_inode_48(db_inode256_reclaimable_ptr &&source_node,
                           optimistic_lock::write_guard &&source_node_guard,
                           std::uint8_t child_to_delete,
                           optimistic_lock::write_guard &&child_guard) noexcept
    : parent_class{
          obsolete_and_move(source_node, std::move(source_node_guard)),
          obsolete_child_by_index(child_to_delete, std::move(child_guard))} {
  assert(!source_node_guard.active());
  assert(!child_guard.active());
}

template <class INode>
[[nodiscard]] std::optional<in_critical_section<olc_node_ptr> *>
olc_impl_helpers::add_or_choose_subtree(
    INode &inode, std::byte key_byte, art_key k, value_view v,
    olc_db &db_instance, tree_depth depth,
    optimistic_lock::read_critical_section &node_critical_section,
    in_critical_section<olc_node_ptr> *node_in_parent,
    optimistic_lock::read_critical_section &parent_critical_section) {
  auto *const child_in_parent = inode.find_child(key_byte).second;

  if (child_in_parent == nullptr) {
    if (UNODB_DETAIL_UNLIKELY(!node_critical_section.check())) return {};
    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.check())) return {};

    auto leaf{olc_art_policy::make_db_leaf_ptr(k, v, db_instance)};

    const auto children_count = inode.get_children_count();

    if constexpr (!std::is_same_v<INode, olc_inode_256>) {
      if (UNODB_DETAIL_UNLIKELY(children_count == INode::capacity)) {
        // TODO(laurynas): shorten the critical section by moving allocation
        // before it?
        optimistic_lock::write_guard write_unlock_on_exit{
            std::move(parent_critical_section)};
        if (UNODB_DETAIL_UNLIKELY(write_unlock_on_exit.must_restart()))
          return {};  // LCOV_EXCL_LINE

        optimistic_lock::write_guard node_write_guard{
            std::move(node_critical_section)};
        if (UNODB_DETAIL_UNLIKELY(node_write_guard.must_restart())) return {};

        auto current_node{make_db_inode_reclaimable_ptr(db_instance, &inode)};
        auto larger_node{INode::larger_derived_type::create(
            std::move(current_node), std::move(node_write_guard),
            std::move(leaf), depth)};
        *node_in_parent = detail::olc_node_ptr{larger_node.release()};
        db_instance.account_growing_inode<INode::larger_derived_type::type>();

        assert(!node_write_guard.active());

        return child_in_parent;
      }
    }

    optimistic_lock::write_guard write_unlock_on_exit{
        std::move(node_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(write_unlock_on_exit.must_restart())) return {};

    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    inode.add_to_nonfull(std::move(leaf), depth, children_count);
  }

  return child_in_parent;
}

template <class INode>
[[nodiscard]] std::optional<bool> olc_impl_helpers::remove_or_choose_subtree(
    INode &inode, std::byte key_byte, detail::art_key k, olc_db &db_instance,
    optimistic_lock::read_critical_section &parent_critical_section,
    optimistic_lock::read_critical_section &node_critical_section,
    in_critical_section<olc_node_ptr> *node_in_parent,
    in_critical_section<olc_node_ptr> **child_in_parent,
    optimistic_lock::read_critical_section *child_critical_section,
    node_type *child_type, olc_node_ptr *child) {
  auto [child_i, found_child]{inode.find_child(key_byte)};

  if (found_child == nullptr) {
    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE
    if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    return false;
  }

  *child = found_child->load();

  if (UNODB_DETAIL_UNLIKELY(!node_critical_section.check())) return {};

  auto &child_lock{node_ptr_lock(*child)};
  *child_critical_section = child_lock.try_read_lock();
  if (UNODB_DETAIL_UNLIKELY(child_critical_section->must_restart())) return {};

  *child_type = child->type();

  if (*child_type != node_type::LEAF) {
    *child_in_parent = found_child;
    return true;
  }

  if (!leaf::matches(child->as_leaf(), k)) {
    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE
    if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE
    if (UNODB_DETAIL_UNLIKELY(!child_critical_section->try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    return false;
  }

  const auto is_node_min_size{inode.is_min_size()};

  if (UNODB_DETAIL_LIKELY(!is_node_min_size)) {
    optimistic_lock::write_guard node_guard{std::move(node_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

    optimistic_lock::write_guard child_guard{
        std::move(*child_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(child_guard.must_restart())) return {};

    child_guard.unlock_and_obsolete();

    inode.remove(child_i, db_instance);

    *child_in_parent = nullptr;
    return true;
  }

  assert(is_node_min_size);

  optimistic_lock::write_guard parent_guard{std::move(parent_critical_section)};
  if (UNODB_DETAIL_UNLIKELY(parent_guard.must_restart())) return {};

  optimistic_lock::write_guard node_guard{std::move(node_critical_section)};
  if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

  optimistic_lock::write_guard child_guard{std::move(*child_critical_section)};
  if (UNODB_DETAIL_UNLIKELY(child_guard.must_restart())) {
    // LCOV_EXCL_START
    node_guard.unlock();
    return {};
    // LCOV_EXCL_STOP
  }

  auto current_node{make_db_inode_reclaimable_ptr(db_instance, &inode)};
  if constexpr (std::is_same_v<INode, olc_inode_4>) {
    node_guard.unlock_and_obsolete();
    child_guard.unlock_and_obsolete();
    *node_in_parent = current_node->leave_last_child(child_i, db_instance);
  } else {
    auto new_node{INode::smaller_derived_type::create(
        std::move(current_node), std::move(node_guard), child_i,
        std::move(child_guard))};
    *node_in_parent = detail::olc_node_ptr{new_node.release()};
  }
  db_instance.template account_shrinking_inode<INode::type>();

  assert(!node_guard.active());
  assert(!child_guard.active());

  *child_in_parent = nullptr;
  return true;
}

}  // namespace unodb::detail

namespace unodb {

template <class INode>
constexpr void olc_db::decrement_inode_count() noexcept {
  static_assert(detail::olc_inode_defs::is_inode<INode>());

  const auto UNODB_DETAIL_USED_IN_DEBUG old_inode_count =
      node_counts[as_i<INode::type>].fetch_sub(1, std::memory_order_relaxed);
  assert(old_inode_count > 0);

  decrease_memory_use(sizeof(INode));
}

template <node_type NodeType>
constexpr void olc_db::account_growing_inode() noexcept {
  static_assert(NodeType != node_type::LEAF);

  growing_inode_counts[internal_as_i<NodeType>].fetch_add(
      1, std::memory_order_relaxed);
}

template <node_type NodeType>
constexpr void olc_db::account_shrinking_inode() noexcept {
  static_assert(NodeType != node_type::LEAF);

  shrinking_inode_counts[internal_as_i<NodeType>].fetch_add(
      1, std::memory_order_relaxed);
}

olc_db::~olc_db() noexcept {
  assert(qsbr::instance().single_thread_mode());

  delete_root_subtree();
}

olc_db::get_result olc_db::get(key search_key) const noexcept {
  try_get_result_type result;
  const detail::art_key bin_comparable_key{search_key};
  do {
    result = try_get(bin_comparable_key);
    // TODO(laurynas): upgrade to write locks to prevent starving after a
    // certain number of failures?
  } while (!result);

  return *result;
}

olc_db::try_get_result_type olc_db::try_get(detail::art_key k) const noexcept {
  auto parent_critical_section = root_pointer_lock.try_read_lock();
  if (UNODB_DETAIL_UNLIKELY(parent_critical_section.must_restart())) return {};

  auto node{root.load()};

  if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.check())) return {};

  if (UNODB_DETAIL_UNLIKELY(node == nullptr)) {
    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE
    return std::make_optional<get_result>(std::nullopt);
  }

  auto remaining_key{k};

  while (true) {
    auto node_critical_section = node_ptr_lock(node).try_read_lock();
    if (UNODB_DETAIL_UNLIKELY(node_critical_section.must_restart())) return {};

    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    const auto node_type = node.type();

    if (node_type == node_type::LEAF) {
      const auto *const leaf{node.as_leaf()};
      if (leaf::matches(leaf, k)) {
        const auto value{leaf::value(leaf)};
        if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
          return {};  // LCOV_EXCL_LINE
        return qsbr_ptr_span<const std::byte>{value};
      }
      if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE
      return std::make_optional<get_result>(std::nullopt);
    }

    auto *const inode{node.as_inode()};
    const auto key_prefix_length{inode->get_key_prefix().length()};
    const auto shared_key_prefix_length{
        inode->get_key_prefix().get_shared_length(remaining_key)};

    if (shared_key_prefix_length < key_prefix_length) {
      if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE
      return std::make_optional<get_result>(std::nullopt);
    }

    if (UNODB_DETAIL_UNLIKELY(!node_critical_section.check())) return {};

    assert(shared_key_prefix_length == key_prefix_length);

    remaining_key.shift_right(key_prefix_length);

    auto *const child_in_parent{
        inode->find_child(node_type, remaining_key[0]).second};

    if (child_in_parent == nullptr) {
      if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE
      return std::make_optional<get_result>(std::nullopt);
    }

    const auto child = child_in_parent->load();

    if (UNODB_DETAIL_UNLIKELY(!node_critical_section.check())) return {};

    parent_critical_section = std::move(node_critical_section);
    node = child;
    remaining_key.shift_right(1);
  }
}

bool olc_db::insert(key insert_key, value_view v) {
  const auto bin_comparable_key = detail::art_key{insert_key};

  try_update_result_type result;
  do {
    result = try_insert(bin_comparable_key, v);
  } while (!result);

  return *result;
}

olc_db::try_update_result_type olc_db::try_insert(detail::art_key k,
                                                  value_view v) {
  auto parent_critical_section = root_pointer_lock.try_read_lock();
  if (UNODB_DETAIL_UNLIKELY(parent_critical_section.must_restart())) return {};

  auto *node_in_parent{&root};
  auto node{root.load()};

  if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.check())) return {};

  if (UNODB_DETAIL_UNLIKELY(node == nullptr)) {
    auto leaf{olc_art_policy::make_db_leaf_ptr(k, v, *this)};

    optimistic_lock::write_guard write_unlock_on_exit{
        std::move(parent_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(write_unlock_on_exit.must_restart())) return {};

    root = detail::olc_node_ptr{leaf.release()};
    return true;
  }

  detail::tree_depth depth{};
  auto remaining_key{k};

  while (true) {
    auto node_critical_section = node_ptr_lock(node).try_read_lock();
    if (UNODB_DETAIL_UNLIKELY(node_critical_section.must_restart())) return {};

    const auto node_type = node.type();

    if (node_type == node_type::LEAF) {
      auto *const leaf{node.as_leaf()};
      const auto existing_key{leaf::key(leaf)};
      if (UNODB_DETAIL_UNLIKELY(k == existing_key)) {
        if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
          return {};  // LCOV_EXCL_LINE
        if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
          return {};  // LCOV_EXCL_LINE

        return false;
      }

      auto new_leaf{olc_art_policy::make_db_leaf_ptr(k, v, *this)};

      optimistic_lock::write_guard parent_guard{
          std::move(parent_critical_section)};
      if (UNODB_DETAIL_UNLIKELY(parent_guard.must_restart())) return {};

      optimistic_lock::write_guard node_guard{std::move(node_critical_section)};
      if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

      // TODO(laurynas): consider creating new lower version and replacing
      // contents, to enable replacing parent write unlock with parent unlock
      auto new_node{detail::olc_inode_4::create(
          existing_key, remaining_key, depth, leaf, std::move(new_leaf))};
      *node_in_parent = detail::olc_node_ptr{new_node.release()};
      account_growing_inode<node_type::I4>();
      return true;
    }

    assert(node_type != node_type::LEAF);
    assert(depth < detail::art_key::size);

    auto *const inode{node.as_inode()};
    const auto key_prefix_length{inode->get_key_prefix().length()};
    const auto shared_prefix_length{
        inode->get_key_prefix().get_shared_length(remaining_key)};

    if (UNODB_DETAIL_UNLIKELY(!node_critical_section.check())) return {};

    if (shared_prefix_length < key_prefix_length) {
      auto leaf{olc_art_policy::make_db_leaf_ptr(k, v, *this)};

      optimistic_lock::write_guard parent_guard{
          std::move(parent_critical_section)};
      if (UNODB_DETAIL_UNLIKELY(parent_guard.must_restart())) return {};

      optimistic_lock::write_guard node_guard{std::move(node_critical_section)};
      if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

      auto new_node = detail::olc_inode_4::create(node, shared_prefix_length,
                                                  depth, std::move(leaf));
      *node_in_parent = detail::olc_node_ptr{new_node.release()};
      account_growing_inode<node_type::I4>();
      key_prefix_splits.fetch_add(1, std::memory_order_relaxed);
      return true;
    }

    assert(shared_prefix_length == key_prefix_length);

    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    const auto add_result{inode->add_or_choose_subtree<
        std::optional<in_critical_section<detail::olc_node_ptr> *>>(
        node_type, remaining_key[0], k, v, *this, depth, node_critical_section,
        node_in_parent, parent_critical_section)};

    if (UNODB_DETAIL_UNLIKELY(!add_result)) return {};

    auto *const child_in_parent = *add_result;
    if (child_in_parent == nullptr) return true;

    const auto child = child_in_parent->load();

    if (UNODB_DETAIL_UNLIKELY(!node_critical_section.check())) return {};
    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    parent_critical_section = std::move(node_critical_section);
    node = child;
    node_in_parent = child_in_parent;
    ++depth;
    remaining_key.shift_right(1);
  }
}

bool olc_db::remove(key remove_key) {
  const auto bin_comparable_key = detail::art_key{remove_key};

  try_update_result_type result;
  do {
    result = try_remove(bin_comparable_key);
  } while (!result);

  return *result;
}

olc_db::try_update_result_type olc_db::try_remove(detail::art_key k) {
  auto parent_critical_section = root_pointer_lock.try_read_lock();
  if (UNODB_DETAIL_UNLIKELY(parent_critical_section.must_restart())) return {};

  auto *node_in_parent{&root};
  auto node{root.load()};

  if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.check())) return {};

  if (UNODB_DETAIL_UNLIKELY(node == nullptr)) return false;

  auto node_critical_section = node_ptr_lock(node).try_read_lock();
  if (UNODB_DETAIL_UNLIKELY(node_critical_section.must_restart())) return {};

  auto node_type = node.type();

  if (UNODB_DETAIL_UNLIKELY(!node_critical_section.check())) return {};

  if (node_type == node_type::LEAF) {
    auto *const leaf{node.as_leaf()};
    if (leaf::matches(leaf, k)) {
      optimistic_lock::write_guard parent_guard{
          std::move(parent_critical_section)};
      if (UNODB_DETAIL_UNLIKELY(parent_guard.must_restart())) return {};

      optimistic_lock::write_guard node_guard{std::move(node_critical_section)};
      if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

      node_guard.unlock_and_obsolete();

      const auto r{olc_art_policy::reclaim_leaf_on_scope_exit(leaf, *this)};
      root = detail::olc_node_ptr{nullptr};
      return true;
    }

    if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    return false;
  }

  detail::tree_depth depth{};
  auto remaining_key{k};

  while (true) {
    assert(node_type != node_type::LEAF);
    assert(depth < detail::art_key::size);

    auto *const inode{node.as_inode()};
    const auto key_prefix_length{inode->get_key_prefix().length()};
    const auto shared_prefix_length{
        inode->get_key_prefix().get_shared_length(remaining_key)};

    if (shared_prefix_length < key_prefix_length) {
      if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE
      if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE

      return false;
    }

    if (UNODB_DETAIL_UNLIKELY(!node_critical_section.check())) return {};

    assert(shared_prefix_length == key_prefix_length);
    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    in_critical_section<detail::olc_node_ptr> *child_in_parent;
    enum node_type child_type;
    detail::olc_node_ptr child;
    optimistic_lock::read_critical_section child_critical_section;

    const auto opt_remove_result{
        inode->remove_or_choose_subtree<std::optional<bool>>(
            node_type, remaining_key[0], k, *this, parent_critical_section,
            node_critical_section, node_in_parent, &child_in_parent,
            &child_critical_section, &child_type, &child)};

    if (UNODB_DETAIL_UNLIKELY(!opt_remove_result)) return {};

    const auto remove_result{*opt_remove_result};

    if (UNODB_DETAIL_UNLIKELY(!remove_result)) return false;
    if (child_in_parent == nullptr) return true;

    parent_critical_section = std::move(node_critical_section);
    node = child;
    node_in_parent = child_in_parent;
    node_critical_section = std::move(child_critical_section);
    node_type = child_type;

    ++depth;
    remaining_key.shift_right(1);
  }
}

void olc_db::delete_root_subtree() noexcept {
  assert(qsbr::instance().single_thread_mode());

  if (root != nullptr) olc_art_policy::delete_subtree(root, *this);
  // It is possible to reset the counter to zero instead of decrementing it for
  // each leaf, but not sure the savings will be significant.
  assert(node_counts[as_i<node_type::LEAF>].load(std::memory_order_relaxed) ==
         0);
}

void olc_db::clear() {
  assert(qsbr::instance().single_thread_mode());

  delete_root_subtree();

  root = detail::olc_node_ptr{nullptr};
  current_memory_use.store(0, std::memory_order_relaxed);

  node_counts[as_i<node_type::I4>].store(0, std::memory_order_relaxed);
  node_counts[as_i<node_type::I16>].store(0, std::memory_order_relaxed);
  node_counts[as_i<node_type::I48>].store(0, std::memory_order_relaxed);
  node_counts[as_i<node_type::I256>].store(0, std::memory_order_relaxed);
}

UNODB_DETAIL_DISABLE_GCC_WARNING("-Wsuggest-attribute=cold")

void olc_db::increase_memory_use(std::size_t delta) {
  if (delta == 0) return;

  current_memory_use.fetch_add(delta, std::memory_order_relaxed);
}

UNODB_DETAIL_RESTORE_GCC_WARNINGS()

void olc_db::decrease_memory_use(std::size_t delta) noexcept {
  if (delta == 0) return;

  assert(delta <= current_memory_use.load(std::memory_order_relaxed));
  current_memory_use.fetch_sub(delta, std::memory_order_relaxed);
}

void olc_db::dump(std::ostream &os) const {
  os << "olc_db dump, currently used = " << get_current_memory_use() << '\n';
  detail::dump_node(os, root.load());
}

}  // namespace unodb
