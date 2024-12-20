// Copyright 2019-2024 Laurynas Biveinis

#include "global.hpp"

// IWYU pragma: no_include <__ostream/basic_ostream.h>

#include "olc_art.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>  // IWYU pragma: keep
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#ifdef UNODB_DETAIL_WITH_STATS
#include <atomic>
#endif

#include "art_common.hpp"
#include "art_internal_impl.hpp"
#include "assert.hpp"
#include "node_type.hpp"
#include "optimistic_lock.hpp"
#include "qsbr.hpp"
#include "qsbr_ptr.hpp"

namespace unodb::detail {

struct [[nodiscard]] olc_node_header {
  [[nodiscard]] constexpr optimistic_lock &lock() const noexcept {
    return m_lock;
  }

#ifndef NDEBUG
  static void check_on_dealloc(const void *ptr) noexcept {
    static_cast<const olc_node_header *>(ptr)->m_lock.check_on_dealloc();
  }
#endif

 private:
  mutable optimistic_lock m_lock;
};

static_assert(std::is_standard_layout_v<olc_node_header>);

template <class Header, class Db>
class db_leaf_qsbr_deleter {
 public:
  using leaf_type = basic_leaf<Header>;
  static_assert(std::is_trivially_destructible_v<leaf_type>);

  constexpr explicit db_leaf_qsbr_deleter(
      Db &db_ UNODB_DETAIL_LIFETIMEBOUND) noexcept
      : db_instance{db_} {}

  void operator()(leaf_type *to_delete) const {
#ifdef UNODB_DETAIL_WITH_STATS
    const auto leaf_size = to_delete->get_size();
#endif  // UNODB_DETAIL_WITH_STATS

    this_thread().on_next_epoch_deallocate(to_delete
#ifdef UNODB_DETAIL_WITH_STATS
                                           ,
                                           leaf_size
#endif  // UNODB_DETAIL_WITH_STATS
#ifndef NDEBUG
                                           ,
                                           olc_node_header::check_on_dealloc
#endif
    );

#ifdef UNODB_DETAIL_WITH_STATS
    db_instance.decrement_leaf_count(leaf_size);
#endif  // UNODB_DETAIL_WITH_STATS
  }

  ~db_leaf_qsbr_deleter() = default;
  db_leaf_qsbr_deleter(const db_leaf_qsbr_deleter<Header, Db> &) = default;
  db_leaf_qsbr_deleter<Header, Db> &operator=(
      const db_leaf_qsbr_deleter<Header, Db> &) = delete;
  db_leaf_qsbr_deleter(db_leaf_qsbr_deleter<Header, Db> &&) = delete;
  db_leaf_qsbr_deleter<Header, Db> &operator=(
      db_leaf_qsbr_deleter<Header, Db> &&) = delete;

 private:
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
  Db &db_instance;
};

}  // namespace unodb::detail

namespace {

template <class INode>
using db_inode_qsbr_deleter_parent =
    unodb::detail::basic_db_inode_deleter<INode, unodb::olc_db>;

}  // namespace

namespace unodb::detail {

template <class INode>
class db_inode_qsbr_deleter : public db_inode_qsbr_deleter_parent<INode> {
 public:
  using db_inode_qsbr_deleter_parent<INode>::db_inode_qsbr_deleter_parent;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)
  void operator()(INode *inode_ptr) {
    static_assert(std::is_trivially_destructible_v<INode>);

    this_thread().on_next_epoch_deallocate(inode_ptr
#ifdef UNODB_DETAIL_WITH_STATS
                                           ,
                                           sizeof(INode)
#endif
#ifndef NDEBUG
                                               ,
                                           olc_node_header::check_on_dealloc
#endif
    );

#ifdef UNODB_DETAIL_WITH_STATS
    this->get_db().template decrement_inode_count<INode>();
#endif  // UNODB_DETAIL_WITH_STATS
  }
  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
};

}  // namespace unodb::detail

namespace {

class olc_inode;
class olc_inode_4;
class olc_inode_16;
class olc_inode_48;
class olc_inode_256;

using olc_inode_defs =
    unodb::detail::basic_inode_def<olc_inode, olc_inode_4, olc_inode_16,
                                   olc_inode_48, olc_inode_256>;

using olc_art_policy =
    unodb::detail::basic_art_policy<unodb::olc_db, unodb::in_critical_section,
                                    unodb::detail::olc_node_ptr, olc_inode_defs,
                                    unodb::detail::db_inode_qsbr_deleter,
                                    unodb::detail::db_leaf_qsbr_deleter>;

using olc_db_leaf_unique_ptr = olc_art_policy::db_leaf_unique_ptr;

using leaf = olc_art_policy::leaf_type;

using olc_inode_base = unodb::detail::basic_inode_impl<olc_art_policy>;

class olc_inode : public olc_inode_base {};

[[nodiscard]] auto &node_ptr_lock(
    const unodb::detail::olc_node_ptr
        &node UNODB_DETAIL_LIFETIMEBOUND) noexcept {
  return node.ptr<unodb::detail::olc_node_header *>()->lock();
}

#ifndef NDEBUG

[[nodiscard]] auto &node_ptr_lock(
    const leaf *const node UNODB_DETAIL_LIFETIMEBOUND) noexcept {
  return node->lock();
}

#endif

template <class INode>
[[nodiscard]] constexpr auto &lock(
    const INode &inode UNODB_DETAIL_LIFETIMEBOUND) noexcept {
  return inode.lock();
}

template <class T>
[[nodiscard]] T &obsolete(T &t UNODB_DETAIL_LIFETIMEBOUND,
                          unodb::optimistic_lock::write_guard &guard) noexcept {
  UNODB_DETAIL_ASSERT(guard.guards(lock(t)));

  // My first attempt was to pass guard by value and let it destruct at the end
  // of this scope, but due to copy elision (?) the destructor got called way
  // too late, after the owner node was destructed.
  guard.unlock_and_obsolete();

  return t;
}

[[nodiscard]] inline auto obsolete_child_by_index(
    std::uint8_t child UNODB_DETAIL_LIFETIMEBOUND,
    unodb::optimistic_lock::write_guard &guard) noexcept {
  guard.unlock_and_obsolete();

  return child;
}

}  // namespace

#ifdef UNODB_DETAIL_WITH_STATS

namespace unodb {

template <class INode>
constexpr void olc_db::increment_inode_count() noexcept {
  static_assert(olc_inode_defs::is_inode<INode>());

  node_counts[as_i<INode::type>].fetch_add(1, std::memory_order_relaxed);
  increase_memory_use(sizeof(INode));
}

}  // namespace unodb

#endif  // UNODB_DETAIL_WITH_STATS

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
      optimistic_lock::read_critical_section &parent_critical_section,
      olc_db_leaf_unique_ptr &cached_leaf);

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

}  // namespace unodb::detail

namespace {

class [[nodiscard]] olc_inode_4 final
    : public unodb::detail::basic_inode_4<olc_art_policy> {
  using parent_class = basic_inode_4<olc_art_policy>;

 public:
  using parent_class::parent_class;

  void init(db &db_instance, olc_inode_16 &source_node,
            unodb::optimistic_lock::write_guard &source_node_guard,
            std::uint8_t child_to_delete,
            unodb::optimistic_lock::write_guard &child_guard);

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  void init(unodb::detail::art_key k1, unodb::detail::art_key shifted_k2,
            unodb::detail::tree_depth depth, leaf *child1,
            olc_db_leaf_unique_ptr &&child2) noexcept {
    UNODB_DETAIL_ASSERT(node_ptr_lock(child1).is_write_locked());

    parent_class::init(k1, shifted_k2, depth, child1, std::move(child2));
  }

  void init(unodb::detail::olc_node_ptr source_node, unsigned len,
            unodb::detail::tree_depth depth, olc_db_leaf_unique_ptr &&child1) {
    UNODB_DETAIL_ASSERT(node_ptr_lock(source_node).is_write_locked());

    parent_class::init(source_node, len, depth, std::move(child1));
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return unodb::detail::olc_impl_helpers::add_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return unodb::detail::olc_impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  void remove(std::uint8_t child_index, unodb::olc_db &db_instance) noexcept {
    UNODB_DETAIL_ASSERT(::lock(*this).is_write_locked());

    basic_inode_4::remove(child_index, db_instance);
  }

  [[nodiscard]] auto leave_last_child(std::uint8_t child_to_delete,
                                      unodb::olc_db &db_instance) noexcept {
    UNODB_DETAIL_ASSERT(::lock(*this).is_obsoleted_by_this_thread());
    UNODB_DETAIL_ASSERT(node_ptr_lock(children[child_to_delete].load())
                            .is_obsoleted_by_this_thread());

    return basic_inode_4::leave_last_child(child_to_delete, db_instance);
  }

  // cppcheck-suppress duplInheritedMember
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    os << ", ";
    ::lock(*this).dump(os);
    basic_inode_4::dump(os);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
};

// 48 (or 56) == sizeof(inode_4)
#ifndef _MSC_VER
#ifdef NDEBUG
static_assert(sizeof(olc_inode_4) == 48 + 8);
#else
static_assert(sizeof(olc_inode_4) == 48 + 24);
#endif
#else  // #ifndef _MSC_VER
#ifdef NDEBUG
static_assert(sizeof(olc_inode_4) == 56 + 8);
#else
static_assert(sizeof(olc_inode_4) == 56 + 24);
#endif
#endif  // #ifndef _MSC_VER

class [[nodiscard]] olc_inode_16 final
    : public unodb::detail::basic_inode_16<olc_art_policy> {
  using parent_class = basic_inode_16<olc_art_policy>;

 public:
  using parent_class::parent_class;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  void init(db &db_instance, olc_inode_4 &source_node,
            unodb::optimistic_lock::write_guard &source_node_guard,
            olc_db_leaf_unique_ptr &&child,
            unodb::detail::tree_depth depth) noexcept {
    UNODB_DETAIL_ASSERT(source_node_guard.guards(::lock(source_node)));
    parent_class::init(db_instance, obsolete(source_node, source_node_guard),
                       std::move(child), depth);
    UNODB_DETAIL_ASSERT_INACTIVE(source_node_guard);
  }

  void init(db &db_instance, olc_inode_48 &source_node,
            unodb::optimistic_lock::write_guard &source_node_guard,
            std::uint8_t child_to_delete,
            unodb::optimistic_lock::write_guard &child_guard) noexcept;

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return unodb::detail::olc_impl_helpers::add_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return unodb::detail::olc_impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  void remove(std::uint8_t child_index, unodb::olc_db &db_instance) noexcept {
    UNODB_DETAIL_ASSERT(::lock(*this).is_write_locked());

    basic_inode_16::remove(child_index, db_instance);
  }

  // cppcheck-suppress duplInheritedMember
  [[nodiscard]] find_result find_child(std::byte key_byte) noexcept {
#ifdef UNODB_DETAIL_THREAD_SANITIZER
    const auto children_count_ = this->get_children_count();
    for (unsigned i = 0; i < children_count_; ++i)
      if (keys.byte_array[i] == key_byte)
        return std::make_pair(i, &children[i]);
    return parent_class::child_not_found;
#else
    return basic_inode_16::find_child(key_byte);
#endif
  }

  // cppcheck-suppress duplInheritedMember
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    os << ", ";
    ::lock(*this).dump(os);
    basic_inode_16::dump(os);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
};

// 160 == sizeof(inode_16)
#ifdef NDEBUG
static_assert(sizeof(olc_inode_16) == 160 + 16);
#else   // #ifdef NDEBUG
static_assert(sizeof(olc_inode_16) == 160 + 32);
#endif  // #ifdef NDEBUG

UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

void olc_inode_4::init(db &db_instance, olc_inode_16 &source_node,
                       unodb::optimistic_lock::write_guard &source_node_guard,
                       std::uint8_t child_to_delete,
                       unodb::optimistic_lock::write_guard &child_guard) {
  UNODB_DETAIL_ASSERT(source_node_guard.guards(::lock(source_node)));
  UNODB_DETAIL_ASSERT(child_guard.active());

  parent_class::init(db_instance, obsolete(source_node, source_node_guard),
                     obsolete_child_by_index(child_to_delete, child_guard));

  UNODB_DETAIL_ASSERT_INACTIVE(source_node_guard);
  UNODB_DETAIL_ASSERT_INACTIVE(child_guard);
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

class [[nodiscard]] olc_inode_48 final
    : public unodb::detail::basic_inode_48<olc_art_policy> {
  using parent_class = basic_inode_48<olc_art_policy>;

 public:
  using parent_class::parent_class;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  void init(db &db_instance, olc_inode_16 &source_node,
            unodb::optimistic_lock::write_guard &source_node_guard,
            olc_db_leaf_unique_ptr &&child,
            unodb::detail::tree_depth depth) noexcept {
    UNODB_DETAIL_ASSERT(source_node_guard.guards(::lock(source_node)));
    parent_class::init(db_instance, obsolete(source_node, source_node_guard),
                       std::move(child), depth);
    UNODB_DETAIL_ASSERT_INACTIVE(source_node_guard);
  }

  void init(db &db_instance, olc_inode_256 &source_node,
            unodb::optimistic_lock::write_guard &source_node_guard,
            std::uint8_t child_to_delete,
            unodb::optimistic_lock::write_guard &child_guard) noexcept;

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return unodb::detail::olc_impl_helpers::add_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return unodb::detail::olc_impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  void remove(std::uint8_t child_index, unodb::olc_db &db_instance) noexcept {
    UNODB_DETAIL_ASSERT(::lock(*this).is_write_locked());

    basic_inode_48::remove(child_index, db_instance);
  }

  // cppcheck-suppress duplInheritedMember
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    os << ", ";
    ::lock(*this).dump(os);
    basic_inode_48::dump(os);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
};

// sizeof(inode_48) == 672 on AVX2, 656 otherwise
#ifdef NDEBUG
// AVX2 too. Padding?
static_assert(sizeof(olc_inode_48) == 656 + 16);
#else  // #ifdef NDEBUG
#if defined(UNODB_DETAIL_AVX2)
static_assert(sizeof(olc_inode_48) == 672 + 32);
#else
static_assert(sizeof(olc_inode_48) == 656 + 32);
#endif
#endif  // #ifdef NDEBUG

UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

void olc_inode_16::init(
    db &db_instance, olc_inode_48 &source_node,
    unodb::optimistic_lock::write_guard &source_node_guard,
    std::uint8_t child_to_delete,
    unodb::optimistic_lock::write_guard &child_guard) noexcept {
  UNODB_DETAIL_ASSERT(source_node_guard.guards(::lock(source_node)));
  UNODB_DETAIL_ASSERT(child_guard.active());

  parent_class::init(db_instance, obsolete(source_node, source_node_guard),
                     obsolete_child_by_index(child_to_delete, child_guard));

  UNODB_DETAIL_ASSERT_INACTIVE(source_node_guard);
  UNODB_DETAIL_ASSERT_INACTIVE(child_guard);
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

class [[nodiscard]] olc_inode_256 final
    : public unodb::detail::basic_inode_256<olc_art_policy> {
  using parent_class = basic_inode_256<olc_art_policy>;

 public:
  using parent_class::parent_class;

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  void init(db &db_instance, olc_inode_48 &source_node,
            unodb::optimistic_lock::write_guard &source_node_guard,
            olc_db_leaf_unique_ptr &&child,
            unodb::detail::tree_depth depth) noexcept {
    UNODB_DETAIL_ASSERT(source_node_guard.guards(::lock(source_node)));
    parent_class::init(db_instance, obsolete(source_node, source_node_guard),
                       std::move(child), depth);
    UNODB_DETAIL_ASSERT_INACTIVE(source_node_guard);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

  template <typename... Args>
  [[nodiscard]] auto add_or_choose_subtree(Args &&...args) {
    return unodb::detail::olc_impl_helpers::add_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  template <typename... Args>
  [[nodiscard]] auto remove_or_choose_subtree(Args &&...args) {
    return unodb::detail::olc_impl_helpers::remove_or_choose_subtree(
        *this, std::forward<Args>(args)...);
  }

  UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

  void remove(std::uint8_t child_index, unodb::olc_db &db_instance) noexcept {
    UNODB_DETAIL_ASSERT(::lock(*this).is_write_locked());

    basic_inode_256::remove(child_index, db_instance);
  }

  // cppcheck-suppress duplInheritedMember
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    os << ", ";
    ::lock(*this).dump(os);
    basic_inode_256::dump(os);
  }

  UNODB_DETAIL_RESTORE_MSVC_WARNINGS()
};

// 2064 == sizeof(inode_256)
#ifdef NDEBUG
static_assert(sizeof(olc_inode_256) == 2064 + 8);
#else
static_assert(sizeof(olc_inode_256) == 2064 + 24);
#endif

UNODB_DETAIL_DISABLE_MSVC_WARNING(26434)

void olc_inode_48::init(
    db &db_instance, olc_inode_256 &source_node,
    unodb::optimistic_lock::write_guard &source_node_guard,
    std::uint8_t child_to_delete,
    unodb::optimistic_lock::write_guard &child_guard) noexcept {
  UNODB_DETAIL_ASSERT(source_node_guard.guards(::lock(source_node)));
  UNODB_DETAIL_ASSERT(child_guard.active());

  parent_class::init(db_instance, obsolete(source_node, source_node_guard),
                     obsolete_child_by_index(child_to_delete, child_guard));

  UNODB_DETAIL_ASSERT_INACTIVE(source_node_guard);
  UNODB_DETAIL_ASSERT_INACTIVE(child_guard);
}

UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

void create_leaf_if_needed(olc_db_leaf_unique_ptr &cached_leaf,
                           unodb::detail::art_key k, unodb::value_view v,
                           unodb::olc_db &db_instance) {
  if (UNODB_DETAIL_LIKELY(cached_leaf == nullptr)) {
    UNODB_DETAIL_ASSERT(&cached_leaf.get_deleter().get_db() == &db_instance);
    // Do not assign because we do not need to assign the deleter
    // NOLINTNEXTLINE(misc-uniqueptr-reset-release)
    cached_leaf.reset(
        olc_art_policy::make_db_leaf_ptr(k, v, db_instance).release());
  }
}

}  // namespace

namespace unodb::detail {

UNODB_DETAIL_DISABLE_MSVC_WARNING(26460)
template <class INode>
[[nodiscard]] std::optional<in_critical_section<olc_node_ptr> *>
olc_impl_helpers::add_or_choose_subtree(
    INode &inode, std::byte key_byte, art_key k, value_view v,
    olc_db &db_instance, tree_depth depth,
    optimistic_lock::read_critical_section &node_critical_section,
    in_critical_section<olc_node_ptr> *node_in_parent,
    optimistic_lock::read_critical_section &parent_critical_section,
    olc_db_leaf_unique_ptr &cached_leaf) {
  auto *const child_in_parent = inode.find_child(key_byte).second;

  if (child_in_parent == nullptr) {
    create_leaf_if_needed(cached_leaf, k, v, db_instance);

    const auto children_count = inode.get_children_count();

    if constexpr (!std::is_same_v<INode, olc_inode_256>) {
      if (UNODB_DETAIL_UNLIKELY(children_count == INode::capacity)) {
        auto larger_node{
            INode::larger_derived_type::create(db_instance, inode)};
        {
          const optimistic_lock::write_guard write_unlock_on_exit{
              std::move(parent_critical_section)};
          if (UNODB_DETAIL_UNLIKELY(write_unlock_on_exit.must_restart()))
            return {};  // LCOV_EXCL_LINE

          optimistic_lock::write_guard node_write_guard{
              std::move(node_critical_section)};
          if (UNODB_DETAIL_UNLIKELY(node_write_guard.must_restart())) return {};

          larger_node->init(db_instance, inode, node_write_guard,
                            std::move(cached_leaf), depth);
          *node_in_parent = detail::olc_node_ptr{
              larger_node.release(), INode::larger_derived_type::type};

          UNODB_DETAIL_ASSERT_INACTIVE(node_write_guard);
        }

#ifdef UNODB_DETAIL_WITH_STATS
        db_instance.account_growing_inode<INode::larger_derived_type::type>();
#endif  // UNODB_DETAIL_WITH_STATS

        return child_in_parent;
      }
    }

    const optimistic_lock::write_guard write_unlock_on_exit{
        std::move(node_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(write_unlock_on_exit.must_restart())) return {};

    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    inode.add_to_nonfull(std::move(cached_leaf), depth, children_count);
  }

  return child_in_parent;
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

template <class INode>
[[nodiscard]] std::optional<bool> olc_impl_helpers::remove_or_choose_subtree(
    INode &inode, std::byte key_byte, detail::art_key k, olc_db &db_instance,
    optimistic_lock::read_critical_section &parent_critical_section,
    optimistic_lock::read_critical_section &node_critical_section,
    in_critical_section<olc_node_ptr> *node_in_parent,
    in_critical_section<olc_node_ptr> **child_in_parent,
    optimistic_lock::read_critical_section *child_critical_section,
    node_type *child_type, olc_node_ptr *child) {
  const auto [child_i, found_child]{inode.find_child(key_byte)};

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
    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE
    return true;
  }

  const auto *const leaf{child->ptr<::leaf *>()};
  if (!leaf->matches(k)) {
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
    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    const optimistic_lock::write_guard node_guard{
        std::move(node_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

    optimistic_lock::write_guard child_guard{
        std::move(*child_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(child_guard.must_restart())) return {};

    child_guard.unlock_and_obsolete();

    inode.remove(child_i, db_instance);

    *child_in_parent = nullptr;
    return true;
  }

  UNODB_DETAIL_ASSERT(is_node_min_size);

  if constexpr (std::is_same_v<INode, olc_inode_4>) {
    const optimistic_lock::write_guard parent_guard{
        std::move(parent_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(parent_guard.must_restart())) return {};

    optimistic_lock::write_guard node_guard{std::move(node_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

    optimistic_lock::write_guard child_guard{
        std::move(*child_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(child_guard.must_restart())) return {};

    auto current_node{
        olc_art_policy::make_db_inode_reclaimable_ptr(&inode, db_instance)};
    node_guard.unlock_and_obsolete();
    child_guard.unlock_and_obsolete();
    *node_in_parent = current_node->leave_last_child(child_i, db_instance);

    UNODB_DETAIL_ASSERT_INACTIVE(node_guard);
    UNODB_DETAIL_ASSERT_INACTIVE(child_guard);

    *child_in_parent = nullptr;
  } else {
    auto smaller_node{INode::smaller_derived_type::create(db_instance, inode)};

    const optimistic_lock::write_guard parent_guard{
        std::move(parent_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(parent_guard.must_restart())) return {};

    optimistic_lock::write_guard node_guard{std::move(node_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

    optimistic_lock::write_guard child_guard{
        std::move(*child_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(child_guard.must_restart())) return {};

    smaller_node->init(db_instance, inode, node_guard, child_i, child_guard);
    *node_in_parent = detail::olc_node_ptr{smaller_node.release(),
                                           INode::smaller_derived_type::type};

    UNODB_DETAIL_ASSERT_INACTIVE(node_guard);
    UNODB_DETAIL_ASSERT_INACTIVE(child_guard);

    *child_in_parent = nullptr;
  }

#ifdef UNODB_DETAIL_WITH_STATS
  db_instance.template account_shrinking_inode<INode::type>();
#endif  // UNODB_DETAIL_WITH_STATS

  return true;
}

}  // namespace unodb::detail

namespace unodb {

#ifdef UNODB_DETAIL_WITH_STATS

UNODB_DETAIL_DISABLE_MSVC_WARNING(4189)
template <class INode>
constexpr void olc_db::decrement_inode_count() noexcept {
  static_assert(olc_inode_defs::is_inode<INode>());

  const auto old_inode_count UNODB_DETAIL_USED_IN_DEBUG =
      node_counts[as_i<INode::type>].fetch_sub(1, std::memory_order_relaxed);
  UNODB_DETAIL_ASSERT(old_inode_count > 0);

  decrease_memory_use(sizeof(INode));
}
UNODB_DETAIL_RESTORE_MSVC_WARNINGS()

template <node_type NodeType>
constexpr void olc_db::account_growing_inode() noexcept {
  static_assert(NodeType != node_type::LEAF);

  // NOLINTNEXTLINE(google-readability-casting)
  growing_inode_counts[internal_as_i<NodeType>].fetch_add(
      1, std::memory_order_relaxed);
}

template <node_type NodeType>
constexpr void olc_db::account_shrinking_inode() noexcept {
  static_assert(NodeType != node_type::LEAF);

  shrinking_inode_counts[internal_as_i<NodeType>].fetch_add(
      1, std::memory_order_relaxed);
}

#endif  // UNODB_DETAIL_WITH_STATS

olc_db::~olc_db() noexcept {
  UNODB_DETAIL_ASSERT(
      qsbr_state::single_thread_mode(qsbr::instance().get_state()));

  delete_root_subtree();
}

olc_db::get_result olc_db::get(key search_key) const noexcept {
  try_get_result_type result;
  const detail::art_key bin_comparable_key{search_key};

  while (true) {
    result = try_get(bin_comparable_key);
    if (result) break;
    // TODO(laurynas): upgrade to write locks to prevent starving after a
    // certain number of failures?
  }

  return *result;
}

olc_db::try_get_result_type olc_db::try_get(detail::art_key k) const noexcept {
  auto parent_critical_section = root_pointer_lock.try_read_lock();
  if (UNODB_DETAIL_UNLIKELY(parent_critical_section.must_restart())) {
    // LCOV_EXCL_START
    spin_wait_loop_body();
    return {};
    // LCOV_EXCL_STOP
  }

  auto node{root.load()};

  if (UNODB_DETAIL_UNLIKELY(node == nullptr)) {
    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock())) {
      // LCOV_EXCL_START
      spin_wait_loop_body();
      return {};
      // LCOV_EXCL_STOP
    }
    return std::make_optional<get_result>(std::nullopt);
  }

  auto remaining_key{k};

  if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.check())) {
    // LCOV_EXCL_START
    spin_wait_loop_body();
    return {};
    // LCOV_EXCL_STOP
  }

  while (true) {
    auto node_critical_section = node_ptr_lock(node).try_read_lock();
    if (UNODB_DETAIL_UNLIKELY(node_critical_section.must_restart())) return {};

    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    const auto node_type = node.type();

    if (node_type == node_type::LEAF) {
      const auto *const leaf{node.ptr<::leaf *>()};
      if (leaf->matches(k)) {
        const auto val_view{leaf->get_value_view()};
        if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
          return {};  // LCOV_EXCL_LINE
        return qsbr_ptr_span<const std::byte>{val_view};
      }
      if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE
      return std::make_optional<get_result>(std::nullopt);
    }

    auto *const inode{node.ptr<olc_inode *>()};
    const auto &key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    const auto shared_key_prefix_length{
        key_prefix.get_shared_length(remaining_key)};

    if (shared_key_prefix_length < key_prefix_length) {
      if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE
      return std::make_optional<get_result>(std::nullopt);
    }

    UNODB_DETAIL_ASSERT(shared_key_prefix_length == key_prefix_length);

    remaining_key.shift_right(key_prefix_length);

    const auto *const child_in_parent{
        inode->find_child(node_type, remaining_key[0]).second};

    if (child_in_parent == nullptr) {
      if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE
      return std::make_optional<get_result>(std::nullopt);
    }

    const auto child = child_in_parent->load();

    parent_critical_section = std::move(node_critical_section);
    node = child;
    remaining_key.shift_right(1);

    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.check())) return {};
  }
}

bool olc_db::insert(key insert_key, value_view v) {
  const auto bin_comparable_key = detail::art_key{insert_key};

  try_update_result_type result;
  olc_db_leaf_unique_ptr cached_leaf{
      nullptr,
      detail::basic_db_leaf_deleter<detail::olc_node_header, olc_db>{*this}};

  while (true) {
    result = try_insert(bin_comparable_key, v, cached_leaf);
    if (result) break;
  }

  return *result;
}

olc_db::try_update_result_type olc_db::try_insert(
    detail::art_key k, value_view v, olc_db_leaf_unique_ptr &cached_leaf) {
  auto parent_critical_section = root_pointer_lock.try_read_lock();
  if (UNODB_DETAIL_UNLIKELY(parent_critical_section.must_restart())) {
    // LCOV_EXCL_START
    spin_wait_loop_body();
    return {};
    // LCOV_EXCL_STOP
  }

  auto node{root.load()};

  if (UNODB_DETAIL_UNLIKELY(node == nullptr)) {
    create_leaf_if_needed(cached_leaf, k, v, *this);

    const optimistic_lock::write_guard write_unlock_on_exit{
        std::move(parent_critical_section)};
    if (UNODB_DETAIL_UNLIKELY(write_unlock_on_exit.must_restart())) {
      // Do not call spin_wait_loop_body here - creating the leaf took some time
      return {};  // LCOV_EXCL_LINE
    }

    root = detail::olc_node_ptr{cached_leaf.release(), node_type::LEAF};
    return true;
  }

  auto *node_in_parent{&root};
  detail::tree_depth depth{};
  auto remaining_key{k};

  if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.check())) {
    // LCOV_EXCL_START
    spin_wait_loop_body();
    return {};
    // LCOV_EXCL_STOP
  }

  while (true) {
    auto node_critical_section = node_ptr_lock(node).try_read_lock();
    if (UNODB_DETAIL_UNLIKELY(node_critical_section.must_restart())) return {};

    const auto node_type = node.type();

    if (node_type == node_type::LEAF) {
      auto *const leaf{node.ptr<::leaf *>()};
      const auto existing_key{leaf->get_key()};
      if (UNODB_DETAIL_UNLIKELY(k == existing_key)) {
        if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
          return {};  // LCOV_EXCL_LINE
        if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
          return {};  // LCOV_EXCL_LINE

        if (UNODB_DETAIL_UNLIKELY(cached_leaf != nullptr)) {
          cached_leaf.reset();  // LCOV_EXCL_LINE
        }
        return false;
      }

      create_leaf_if_needed(cached_leaf, k, v, *this);
      auto new_node{
          olc_inode_4::create(*this, existing_key, remaining_key, depth)};

      {
        const optimistic_lock::write_guard parent_guard{
            std::move(parent_critical_section)};
        if (UNODB_DETAIL_UNLIKELY(parent_guard.must_restart())) return {};

        const optimistic_lock::write_guard node_guard{
            std::move(node_critical_section)};
        if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

        new_node->init(existing_key, remaining_key, depth, leaf,
                       std::move(cached_leaf));
        *node_in_parent =
            detail::olc_node_ptr{new_node.release(), node_type::I4};
      }
#ifdef UNODB_DETAIL_WITH_STATS
      account_growing_inode<node_type::I4>();
#endif  // UNODB_DETAIL_WITH_STATS
      return true;
    }

    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);
    UNODB_DETAIL_ASSERT(depth < detail::art_key::size);

    auto *const inode{node.ptr<olc_inode *>()};
    const auto &key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    const auto shared_prefix_length{
        key_prefix.get_shared_length(remaining_key)};

    if (shared_prefix_length < key_prefix_length) {
      create_leaf_if_needed(cached_leaf, k, v, *this);
      auto new_node{olc_inode_4::create(*this, node, shared_prefix_length)};

      {
        const optimistic_lock::write_guard parent_guard{
            std::move(parent_critical_section)};
        if (UNODB_DETAIL_UNLIKELY(parent_guard.must_restart())) return {};

        const optimistic_lock::write_guard node_guard{
            std::move(node_critical_section)};
        if (UNODB_DETAIL_UNLIKELY(node_guard.must_restart())) return {};

        new_node->init(node, shared_prefix_length, depth,
                       std::move(cached_leaf));
        *node_in_parent =
            detail::olc_node_ptr{new_node.release(), node_type::I4};
      }

#ifdef UNODB_DETAIL_WITH_STATS
      account_growing_inode<node_type::I4>();
      key_prefix_splits.fetch_add(1, std::memory_order_relaxed);
#endif  // UNODB_DETAIL_WITH_STATS

      return true;
    }

    UNODB_DETAIL_ASSERT(shared_prefix_length == key_prefix_length);

    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    const auto add_result{inode->add_or_choose_subtree<
        std::optional<in_critical_section<detail::olc_node_ptr> *>>(
        node_type, remaining_key[0], k, v, *this, depth, node_critical_section,
        node_in_parent, parent_critical_section, cached_leaf)};

    if (UNODB_DETAIL_UNLIKELY(!add_result)) return {};

    auto *const child_in_parent = *add_result;
    if (child_in_parent == nullptr) return true;

    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
      return {};  // LCOV_EXCL_LINE

    const auto child = child_in_parent->load();

    parent_critical_section = std::move(node_critical_section);
    node = child;
    node_in_parent = child_in_parent;
    ++depth;
    remaining_key.shift_right(1);

    if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.check())) return {};
  }
}

bool olc_db::remove(key remove_key) {
  const auto bin_comparable_key = detail::art_key{remove_key};

  try_update_result_type result;
  while (true) {
    result = try_remove(bin_comparable_key);
    if (result) break;
  }

  return *result;
}

olc_db::try_update_result_type olc_db::try_remove(detail::art_key k) {
  auto parent_critical_section = root_pointer_lock.try_read_lock();
  if (UNODB_DETAIL_UNLIKELY(parent_critical_section.must_restart())) {
    // LCOV_EXCL_START
    spin_wait_loop_body();
    return {};
    // LCOV_EXCL_STOP
  }

  auto node{root.load()};

  if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.check())) {
    // LCOV_EXCL_START
    spin_wait_loop_body();
    return {};
    // LCOV_EXCL_STOP
  }

  if (UNODB_DETAIL_UNLIKELY(node == nullptr)) return false;

  auto node_critical_section = node_ptr_lock(node).try_read_lock();
  if (UNODB_DETAIL_UNLIKELY(node_critical_section.must_restart())) {
    // LCOV_EXCL_START
    spin_wait_loop_body();
    return {};
    // LCOV_EXCL_STOP
  }

  auto node_type = node.type();

  if (node_type == node_type::LEAF) {
    auto *const leaf{node.ptr<::leaf *>()};
    if (leaf->matches(k)) {
      const optimistic_lock::write_guard parent_guard{
          std::move(parent_critical_section)};
      // Do not call spin_wait_loop_body from this point on - assume the above
      // took enough time
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

  auto *node_in_parent{&root};
  detail::tree_depth depth{};
  auto remaining_key{k};

  while (true) {
    UNODB_DETAIL_ASSERT(node_type != node_type::LEAF);
    UNODB_DETAIL_ASSERT(depth < detail::art_key::size);

    auto *const inode{node.ptr<olc_inode *>()};
    const auto &key_prefix{inode->get_key_prefix()};
    const auto key_prefix_length{key_prefix.length()};
    const auto shared_prefix_length{
        key_prefix.get_shared_length(remaining_key)};

    if (shared_prefix_length < key_prefix_length) {
      if (UNODB_DETAIL_UNLIKELY(!parent_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE
      if (UNODB_DETAIL_UNLIKELY(!node_critical_section.try_read_unlock()))
        return {};  // LCOV_EXCL_LINE

      return false;
    }

    UNODB_DETAIL_ASSERT(shared_prefix_length == key_prefix_length);
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

    if (const auto remove_result{*opt_remove_result}; !remove_result)
      return false;

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
  UNODB_DETAIL_ASSERT(
      qsbr_state::single_thread_mode(qsbr::instance().get_state()));

  if (root != nullptr) olc_art_policy::delete_subtree(root, *this);

#ifdef UNODB_DETAIL_WITH_STATS
  // It is possible to reset the counter to zero instead of decrementing it for
  // each leaf, but not sure the savings will be significant.
  UNODB_DETAIL_ASSERT(
      node_counts[as_i<node_type::LEAF>].load(std::memory_order_relaxed) == 0);
#endif  // UNODB_DETAIL_WITH_STATS
}

void olc_db::clear() noexcept {
  UNODB_DETAIL_ASSERT(
      qsbr_state::single_thread_mode(qsbr::instance().get_state()));

  delete_root_subtree();

  root = detail::olc_node_ptr{nullptr};

#ifdef UNODB_DETAIL_WITH_STATS
  current_memory_use.store(0, std::memory_order_relaxed);

  node_counts[as_i<node_type::I4>].store(0, std::memory_order_relaxed);
  node_counts[as_i<node_type::I16>].store(0, std::memory_order_relaxed);
  node_counts[as_i<node_type::I48>].store(0, std::memory_order_relaxed);
  node_counts[as_i<node_type::I256>].store(0, std::memory_order_relaxed);
#endif  // UNODB_DETAIL_WITH_STATS
}

#ifdef UNODB_DETAIL_WITH_STATS

UNODB_DETAIL_DISABLE_GCC_WARNING("-Wsuggest-attribute=cold")

void olc_db::increase_memory_use(std::size_t delta) noexcept {
  UNODB_DETAIL_ASSERT(delta > 0);

  current_memory_use.fetch_add(delta, std::memory_order_relaxed);
}

UNODB_DETAIL_RESTORE_GCC_WARNINGS()

void olc_db::decrease_memory_use(std::size_t delta) noexcept {
  UNODB_DETAIL_ASSERT(delta > 0);
  UNODB_DETAIL_ASSERT(delta <=
                      current_memory_use.load(std::memory_order_relaxed));

  current_memory_use.fetch_sub(delta, std::memory_order_relaxed);
}

#endif  // UNODB_DETAIL_WITH_STATS

void olc_db::dump(std::ostream &os) const {
#ifdef UNODB_DETAIL_WITH_STATS
  os << "olc_db dump, current memory use = " << get_current_memory_use()
     << '\n';
#else
  os << "olc_db dump\n";
#endif  // UNODB_DETAIL_WITH_STATS
  olc_art_policy::dump_node(os, root.load());
}

}  // namespace unodb
