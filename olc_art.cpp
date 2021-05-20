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

static void olc_node_header_static_asserts();

struct olc_node_header final {
  constexpr explicit olc_node_header(node_type type_) : m_header{type_} {}

  [[nodiscard]] constexpr auto type() const noexcept { return m_header.type(); }

  [[nodiscard]] constexpr optimistic_lock &lock() const noexcept {
    return m_lock;
  }

  void reinit_in_inode() noexcept { new (&m_lock) optimistic_lock; }

 private:
  // Composition and not inheritance so that std::is_standard_layout holds
  const node_header m_header;
  mutable optimistic_lock m_lock;

  friend void olc_node_header_static_asserts();
};

// LCOV_EXCL_START
DISABLE_WARNING("-Wunused-function")
static void olc_node_header_static_asserts() {
#ifdef NDEBUG
  static_assert(sizeof(olc_node_header) == 16);
#else
  static_assert(sizeof(olc_node_header) == 32);
#endif
  static_assert(std::is_standard_layout_v<olc_node_header>);
  static_assert(offsetof(unodb::detail::olc_node_header, m_lock) == 8);
}
RESTORE_WARNINGS()
// LCOV_EXCL_STOP

template <class Header, class Db>
class db_leaf_qsbr_deleter {
 public:
  static_assert(std::is_trivially_destructible_v<basic_leaf<Header>>);

  // cppcheck-suppress constParameter
  constexpr explicit db_leaf_qsbr_deleter(Db &db_) noexcept
      : db_instance{db_} {}

  void operator()(raw_leaf_ptr to_delete) const noexcept {
    const auto leaf_size = basic_leaf<Header>::size(to_delete);

    unodb::qsbr::instance().on_next_epoch_pool_deallocate(
        get_leaf_node_pool(), to_delete, leaf_size,
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

    unodb::qsbr::instance().on_next_epoch_pool_deallocate(
        inode_pool_getter<INode>::get(), inode_ptr, sizeof(INode),
        unodb::detail::alignment_for_new<INode>());

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
    unodb::olc_db, unodb::critical_section_protected,
    unodb::detail::olc_node_ptr, unodb::detail::db_inode_qsbr_deleter,
    unodb::detail::db_leaf_qsbr_deleter, inode_pool_getter>;

using olc_db_leaf_unique_ptr = olc_art_policy::db_leaf_unique_ptr;

using leaf = olc_art_policy::leaf_type;

using olc_inode_base = unodb::detail::basic_inode_impl<olc_art_policy>;

[[nodiscard]] constexpr auto &node_ptr_lock(
    const unodb::detail::olc_node_ptr &node) noexcept {
  return node.header->lock();
}

template <class INode>
[[nodiscard]] constexpr auto &lock(const INode &inode) noexcept {
  return inode.get_header().lock();
}

template <class T>
std::remove_reference_t<T> &&obsolete_and_move(
    T &&t, unodb::unique_write_lock_obsoleting_guard &&guard) noexcept {
  assert(guard.guards(lock(*t)));

  // My first attempt was to pass guard by value and let it destruct at the end
  // of this scope, but due to copy elision (?) the destructor got called way
  // too late, after the owner node was destructed.
  guard.commit();

  return static_cast<std::remove_reference_t<T> &&>(t);
}

inline auto obsolete_child_by_index(
    std::uint8_t child,
    unodb::unique_write_lock_obsoleting_guard &&guard) noexcept {
  guard.commit();

  return child;
}

template <class INode>
[[nodiscard]] std::optional<bool> olc_inode_add(
    INode &inode, olc_db_leaf_unique_ptr &child,
    unodb::detail::tree_depth depth,
    unodb::optimistic_lock::version_type version,
    const unodb::optimistic_lock &parent_lock,
    unodb::optimistic_lock::version_type parent_version) noexcept {
  const auto children_count = inode.get_children_count();

  if constexpr (!std::is_same_v<INode, unodb::detail::olc_inode_256>)
    if (unlikely(children_count == INode::capacity)) return false;

  auto &node_lock = lock(inode);
  if (unlikely(!node_lock.try_upgrade_to_write_lock(version))) return {};
  unodb::optimistic_write_lock_guard unlock_on_exit{node_lock};

  if (unlikely(!parent_lock.try_read_unlock(parent_version))) return {};

  inode.add_to_nonfull(child, depth, children_count);

  return true;
}

}  // namespace

namespace unodb::detail {

class olc_inode : public olc_inode_base {};

class olc_inode_4 final : public basic_inode_4<olc_art_policy> {
  using parent_class = basic_inode_4<olc_art_policy>;

 public:
  constexpr olc_inode_4(detail::art_key k1, detail::art_key shifted_k2,
                        detail::tree_depth depth, olc_node_ptr child1,
                        olc_db_leaf_unique_ptr &&child2) noexcept
      : parent_class{k1, shifted_k2, depth, child1, std::move(child2)} {
    assert(node_ptr_lock(child1).is_write_locked());
  }

  constexpr olc_inode_4(olc_node_ptr source_node, unsigned len,
                        detail::tree_depth depth,
                        olc_db_leaf_unique_ptr &&child1) noexcept
      : parent_class{source_node, len, depth, std::move(child1)} {
    assert(node_ptr_lock(source_node).is_write_locked());
  }

  // FIXME(laurynas): hide guards in unique_ptr deleters?
  olc_inode_4(
      db_inode16_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&node_obsoleting_guard,
      std::uint8_t child_to_delete,
      unique_write_lock_obsoleting_guard &&child_obsoleting_guard) noexcept;

  [[nodiscard]] static db_inode4_unique_ptr create(
      db_inode16_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&node_obsoleting_guard,
      std::uint8_t child_to_delete,
      unique_write_lock_obsoleting_guard &&child_obsoleting_guard);

  // Create a new node with two given child nodes
  [[nodiscard]] static auto create(detail::art_key k1,
                                   detail::art_key shifted_k2,
                                   detail::tree_depth depth,
                                   olc_node_ptr child1,
                                   olc_db_leaf_unique_ptr &&child2) {
    assert(node_ptr_lock(child1).is_write_locked());

    return basic_inode_4::create(k1, shifted_k2, depth, child1,
                                 std::move(child2));
  }

  // Create a new node, split the key prefix of an existing node, and make the
  // new node contain that existing node and a given new node which caused
  // this key prefix split.
  [[nodiscard]] static auto create(olc_node_ptr source_node, unsigned len,
                                   detail::tree_depth depth,
                                   olc_db_leaf_unique_ptr &&child1) {
    assert(node_ptr_lock(source_node).is_write_locked());

    return basic_inode_4::create(source_node, len, depth, std::move(child1));
  }

  [[nodiscard]] std::optional<bool> add(
      olc_db_leaf_unique_ptr &child, detail::tree_depth depth,
      optimistic_lock::version_type version, const optimistic_lock &parent_lock,
      optimistic_lock::version_type parent_version) noexcept {
    return olc_inode_add(*this, child, depth, version, parent_lock,
                         parent_version);
  }

  void remove(std::uint8_t child_index, olc_db &db_instance) noexcept {
    assert(lock(*this).is_write_locked());

    basic_inode_4::remove(child_index, db_instance);
  }

  void add_two_to_empty(std::byte key1, olc_node_ptr child1, std::byte key2,
                        olc_db_leaf_unique_ptr &&child2) noexcept {
    assert(node_ptr_lock(child1).is_write_locked());

    basic_inode_4::add_two_to_empty(key1, child1, key2, std::move(child2));
  }

  auto leave_last_child(std::uint8_t child_to_delete,
                        olc_db &db_instance) noexcept {
    assert(lock(*this).is_obsoleted_by_this_thread());
    assert(node_ptr_lock(children[child_to_delete].load())
               .is_obsoleted_by_this_thread());

    return basic_inode_4::leave_last_child(child_to_delete, db_instance);
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    os << ", ";
    lock(*this).dump(os);
    basic_inode_4::dump(os);
  }
};

// 48 == sizeof(inode_4)
#ifdef NDEBUG
static_assert(sizeof(olc_inode_4) == 48 + 16);
#else
static_assert(sizeof(olc_inode_4) == 48 + 32);
#endif

class olc_inode_16 final : public basic_inode_16<olc_art_policy> {
  using parent_class = basic_inode_16<olc_art_policy>;

 public:
  olc_inode_16(
      db_inode4_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      olc_db_leaf_unique_ptr &&child, detail::tree_depth depth) noexcept
      : parent_class{obsolete_and_move(source_node,
                                       std::move(source_node_obsoleting_guard)),
                     std::move(child), depth} {
    assert(!source_node_obsoleting_guard.active());
  }

  olc_inode_16(
      db_inode48_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      std::uint8_t child_to_delete,
      unique_write_lock_obsoleting_guard &&child_obsoleting_guard) noexcept;

  // FIXME(laurynas): if cannot hide the guards in unique_ptr, move superclass
  // create methods to db::
  [[nodiscard]] static auto create(
      db_inode4_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      olc_db_leaf_unique_ptr &&child, detail::tree_depth depth) {
    assert(source_node_obsoleting_guard.guards(lock(*source_node)));

    return olc_art_policy::make_db_inode_unique_ptr<olc_inode_16>(
        child.get_deleter().get_db(), std::move(source_node),
        std::move(source_node_obsoleting_guard), std::move(child), depth);
  }

  [[nodiscard]] static db_inode16_unique_ptr create(
      db_inode48_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      std::uint8_t child_to_delete,
      unique_write_lock_obsoleting_guard &&child_obsoleting_guard);

  [[nodiscard]] std::optional<bool> add(
      olc_db_leaf_unique_ptr &child, detail::tree_depth depth,
      optimistic_lock::version_type version, const optimistic_lock &parent_lock,
      optimistic_lock::version_type parent_version) noexcept {
    return olc_inode_add(*this, child, depth, version, parent_lock,
                         parent_version);
  }

  void remove(std::uint8_t child_index, olc_db &db_instance) noexcept {
    assert(lock(*this).is_write_locked());

    basic_inode_16::remove(child_index, db_instance);
  }

  [[nodiscard]] find_result find_child(std::byte key_byte) noexcept {
#ifdef UNODB_THREAD_SANITIZER
    const auto children_count_copy = this->f.f.children_count.load();
    for (unsigned i = 0; i < children_count_copy; ++i)
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
    lock(*this).dump(os);
    basic_inode_16::dump(os);
  }
};

// 160 == sizeof(inode_16)
#ifdef NDEBUG
static_assert(sizeof(olc_inode_16) == 160 + 16);
#else
static_assert(sizeof(olc_inode_16) == 160 + 32);
#endif

olc_inode_4::olc_inode_4(
    db_inode16_reclaimable_ptr &&source_node,
    unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
    std::uint8_t child_to_delete,
    unique_write_lock_obsoleting_guard &&child_obsoleting_guard) noexcept
    : parent_class{obsolete_and_move(source_node,
                                     std::move(source_node_obsoleting_guard)),
                   obsolete_child_by_index(child_to_delete,
                                           std::move(child_obsoleting_guard))} {
  assert(!source_node_obsoleting_guard.active());
  assert(!child_obsoleting_guard.active());
}

[[nodiscard]] inline olc_inode_4::db_inode4_unique_ptr olc_inode_4::create(
    db_inode16_reclaimable_ptr &&source_node,
    unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
    std::uint8_t child_to_delete,
    unique_write_lock_obsoleting_guard &&child_obsoleting_guard) {
  assert(source_node_obsoleting_guard.guards(lock(*source_node)));
  assert(child_obsoleting_guard.active());

  return olc_art_policy::make_db_inode_unique_ptr<olc_inode_4>(
      source_node.get_deleter().get_db(), std::move(source_node),
      std::move(source_node_obsoleting_guard), child_to_delete,
      std::move(child_obsoleting_guard));
}

class olc_inode_48 final : public basic_inode_48<olc_art_policy> {
  using parent_class = basic_inode_48<olc_art_policy>;

 public:
  olc_inode_48(
      db_inode16_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      olc_db_leaf_unique_ptr &&child, detail::tree_depth depth) noexcept
      : parent_class{obsolete_and_move(source_node,
                                       std::move(source_node_obsoleting_guard)),
                     std::move(child), depth} {
    assert(!source_node_obsoleting_guard.active());
  }

  [[nodiscard]] static auto create(
      db_inode16_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      olc_db_leaf_unique_ptr &&child, detail::tree_depth depth) {
    assert(source_node_obsoleting_guard.guards(lock(*source_node)));

    return olc_art_policy::make_db_inode_unique_ptr<olc_inode_48>(
        child.get_deleter().get_db(), std::move(source_node),
        std::move(source_node_obsoleting_guard), std::move(child), depth);
  }

  olc_inode_48(
      db_inode256_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      std::uint8_t child_to_delete,
      unique_write_lock_obsoleting_guard &&child_obsoleting_guard) noexcept;

  [[nodiscard]] static db_inode48_unique_ptr create(
      db_inode256_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      std::uint8_t child_to_delete,
      unique_write_lock_obsoleting_guard &&child_obsoleting_guard);

  [[nodiscard]] std::optional<bool> add(
      olc_db_leaf_unique_ptr &child, detail::tree_depth depth,
      optimistic_lock::version_type version, const optimistic_lock &parent_lock,
      optimistic_lock::version_type parent_version) noexcept {
    return olc_inode_add(*this, child, depth, version, parent_lock,
                         parent_version);
  }

  void remove(std::uint8_t child_index, olc_db &db_instance) noexcept {
    assert(lock(*this).is_write_locked());

    basic_inode_48::remove(child_index, db_instance);
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    os << ", ";
    lock(*this).dump(os);
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
    unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
    std::uint8_t child_to_delete,
    unique_write_lock_obsoleting_guard &&child_obsoleting_guard) {
  assert(source_node_obsoleting_guard.guards(lock(*source_node)));
  // TODO(laurynas): consider asserting that the child guard guards the right
  // lock.
  assert(child_obsoleting_guard.active());

  return olc_art_policy::make_db_inode_unique_ptr<olc_inode_16>(
      source_node.get_deleter().get_db(), std::move(source_node),
      std::move(source_node_obsoleting_guard), child_to_delete,
      std::move(child_obsoleting_guard));
}

olc_inode_16::olc_inode_16(
    db_inode48_reclaimable_ptr &&source_node,
    unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
    std::uint8_t child_to_delete,
    unique_write_lock_obsoleting_guard &&child_obsoleting_guard) noexcept
    : parent_class{obsolete_and_move(source_node,
                                     std::move(source_node_obsoleting_guard)),
                   obsolete_child_by_index(child_to_delete,
                                           std::move(child_obsoleting_guard))} {
  assert(!source_node_obsoleting_guard.active());
  assert(!child_obsoleting_guard.active());
}

class olc_inode_256 final : public basic_inode_256<olc_art_policy> {
  using parent_class = basic_inode_256<olc_art_policy>;

 public:
  olc_inode_256(
      db_inode48_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      olc_db_leaf_unique_ptr &&child, detail::tree_depth depth) noexcept
      : parent_class{obsolete_and_move(source_node,
                                       std::move(source_node_obsoleting_guard)),
                     std::move(child), depth} {
    assert(!source_node_obsoleting_guard.active());
  }

  [[nodiscard]] static auto create(
      db_inode48_reclaimable_ptr &&source_node,
      unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
      olc_db_leaf_unique_ptr &&child, detail::tree_depth depth) {
    assert(source_node_obsoleting_guard.guards(lock(*source_node)));

    return olc_art_policy::make_db_inode_unique_ptr<olc_inode_256>(
        child.get_deleter().get_db(), std::move(source_node),
        std::move(source_node_obsoleting_guard), std::move(child), depth);
  }

  [[nodiscard]] std::optional<bool> add(
      olc_db_leaf_unique_ptr &child, detail::tree_depth depth,
      optimistic_lock::version_type version, const optimistic_lock &parent_lock,
      optimistic_lock::version_type parent_version) noexcept {
    return olc_inode_add(*this, child, depth, version, parent_lock,
                         parent_version);
  }

  void remove(std::uint8_t child_index, olc_db &db_instance) noexcept {
    assert(lock(*this).is_write_locked());

    basic_inode_256::remove(child_index, db_instance);
  }

  [[gnu::cold, gnu::noinline]] void dump(std::ostream &os) const {
    os << ", ";
    lock(*this).dump(os);
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
    unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
    std::uint8_t child_to_delete,
    unique_write_lock_obsoleting_guard &&child_obsoleting_guard) {
  assert(source_node_obsoleting_guard.guards(lock(*source_node)));
  assert(child_obsoleting_guard.active());

  return olc_art_policy::make_db_inode_unique_ptr<olc_inode_48>(
      source_node.get_deleter().get_db(), std::move(source_node),
      std::move(source_node_obsoleting_guard), child_to_delete,
      std::move(child_obsoleting_guard));
}

olc_inode_48::olc_inode_48(
    db_inode256_reclaimable_ptr &&source_node,
    unique_write_lock_obsoleting_guard &&source_node_obsoleting_guard,
    std::uint8_t child_to_delete,
    unique_write_lock_obsoleting_guard &&child_obsoleting_guard) noexcept
    : parent_class{obsolete_and_move(source_node,
                                     std::move(source_node_obsoleting_guard)),
                   obsolete_child_by_index(child_to_delete,
                                           std::move(child_obsoleting_guard))} {
  assert(!source_node_obsoleting_guard.active());
  assert(!child_obsoleting_guard.active());
}

}  // namespace unodb::detail

namespace unodb {

template <class INode>
constexpr void olc_db::decrement_inode_count() noexcept {
  static_assert(detail::olc_inode_defs::is_inode<INode>());

  const auto USED_IN_DEBUG old_inode_count =
      node_counts[as_i<INode::static_node_type>].fetch_sub(
          1, std::memory_order_relaxed);
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
  auto *parent_lock = &root_pointer_lock;
  auto optional_parent_version = parent_lock->try_read_lock();
  if (unlikely(!optional_parent_version)) return {};

  auto parent_version = *optional_parent_version;
  auto node{root.load()};

  if (unlikely(!parent_lock->check(parent_version))) return {};

  if (unlikely(node.header == nullptr)) {
    if (unlikely(!parent_lock->try_read_unlock(parent_version))) return {};
    return std::make_optional<get_result>(std::nullopt);
  }

  auto remaining_key{k};

  while (true) {
    auto &node_lock = node_ptr_lock(node);
    const auto optional_version = node_lock.try_read_lock();
    if (unlikely(!optional_version)) return {};

    const auto version = *optional_version;

    if (unlikely(!parent_lock->try_read_unlock(parent_version))) return {};

    const auto node_type = node.type();

    if (node_type == node_type::LEAF) {
      if (leaf::matches(node.leaf, k)) {
        const auto value = leaf::value(node.leaf);
        if (unlikely(!node_lock.try_read_unlock(version))) return {};
        return qsbr_ptr_span<const std::byte>{value};
      }
      if (unlikely(!node_lock.try_read_unlock(version))) return {};
      return std::make_optional<get_result>(std::nullopt);
    }

    const auto key_prefix_length = node.internal->key_prefix_length();
    const auto shared_key_prefix_length =
        node.internal->get_shared_key_prefix_length(remaining_key);

    if (shared_key_prefix_length < key_prefix_length) {
      if (unlikely(!node_lock.try_read_unlock(version))) return {};
      return std::make_optional<get_result>(std::nullopt);
    }

    if (unlikely(!node_lock.check(version))) return {};

    assert(shared_key_prefix_length == key_prefix_length);

    remaining_key.shift_right(key_prefix_length);

    auto *const child_loc =
        node.internal->find_child(node_type, remaining_key[0]).second;

    if (child_loc == nullptr) {
      if (unlikely(!node_lock.try_read_unlock(version))) return {};
      return std::make_optional<get_result>(std::nullopt);
    }

    const auto child = child_loc->load();

    if (unlikely(!node_lock.check(version))) return {};

    parent_lock = &node_lock;
    parent_version = version;
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
  auto *parent_lock = &root_pointer_lock;
  auto optional_parent_version = parent_lock->try_read_lock();
  if (unlikely(!optional_parent_version)) return {};

  auto parent_version = *optional_parent_version;
  auto *node_loc{&root};
  auto node{root.load()};

  if (unlikely(!parent_lock->check(parent_version))) return {};

  if (unlikely(node.header == nullptr)) {
    auto leaf{olc_art_policy::make_db_leaf_ptr(k, v, *this)};

    if (unlikely(!parent_lock->try_upgrade_to_write_lock(parent_version)))
      return {};  // LCOV_EXCL_LINE
    optimistic_write_lock_guard unlock_on_exit{*parent_lock};

    root = leaf.release();
    return true;
  }

  detail::tree_depth depth{};
  auto remaining_key{k};

  while (true) {
    auto &node_lock = node_ptr_lock(node);
    const auto optional_version = node_lock.try_read_lock();
    if (unlikely(!optional_version)) return {};
    const auto version = *optional_version;

    const auto node_type = node.type();

    if (node_type == node_type::LEAF) {
      const auto existing_key = leaf::key(node.leaf);
      if (unlikely(k == existing_key)) {
        if (unlikely(!parent_lock->try_read_unlock(parent_version))) return {};
        if (unlikely(!node_lock.try_read_unlock(version))) return {};

        return false;
      }

      auto leaf{olc_art_policy::make_db_leaf_ptr(k, v, *this)};

      if (unlikely(!parent_lock->try_upgrade_to_write_lock(parent_version)))
        return {};  // LCOV_EXCL_LINE
      optimistic_write_lock_guard unlock_on_exit{*parent_lock};

      if (unlikely(!node_lock.try_upgrade_to_write_lock(version))) return {};
      optimistic_write_lock_guard unlock_on_exit2{node_lock};

      // TODO(laurynas): consider creating new lower version and replacing
      // contents, to enable replacing parent write unlock with parent unlock
      auto new_node = detail::olc_inode_4::create(existing_key, remaining_key,
                                                  depth, node, std::move(leaf));
      *node_loc = new_node.release();
      account_growing_inode<node_type::I4>();
      return true;
    }

    assert(node_type != node_type::LEAF);
    assert(depth < detail::art_key::size);

    const auto key_prefix_length = node.internal->key_prefix_length();
    const auto shared_prefix_length =
        node.internal->get_shared_key_prefix_length(remaining_key);

    if (unlikely(!node_lock.check(version))) return {};

    if (shared_prefix_length < key_prefix_length) {
      auto leaf{olc_art_policy::make_db_leaf_ptr(k, v, *this)};

      if (unlikely(!parent_lock->try_upgrade_to_write_lock(parent_version)))
        return {};  // LCOV_EXCL_LINE
      optimistic_write_lock_guard unlock_on_exit{*parent_lock};

      if (unlikely(!node_lock.try_upgrade_to_write_lock(version))) return {};
      optimistic_write_lock_guard unlock_on_exit2{node_lock};

      auto new_node = detail::olc_inode_4::create(node, shared_prefix_length,
                                                  depth, std::move(leaf));
      *node_loc = new_node.release();
      account_growing_inode<node_type::I4>();
      key_prefix_splits.fetch_add(1, std::memory_order_relaxed);
      return true;
    }

    assert(shared_prefix_length == key_prefix_length);
    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    auto *const child_loc =
        node.internal->find_child(node_type, remaining_key[0]).second;

    if (child_loc == nullptr) {
      if (unlikely(!node_lock.check(version))) return {};
      if (unlikely(!parent_lock->check(parent_version))) return {};

      auto leaf{olc_art_policy::make_db_leaf_ptr(k, v, *this)};

      const auto add_result = node.internal->add<std::optional<bool>>(
          leaf, depth, version, *parent_lock, parent_version);
      if (unlikely(!add_result)) return {};

      const auto node_is_full = !(*add_result);

      if (likely(!node_is_full)) {
        assert(leaf == nullptr);
        return true;
      }

      assert(node_is_full);
      assert(leaf != nullptr);

      if (!unlikely(parent_lock->try_upgrade_to_write_lock(parent_version)))
        return {};  // LCOV_EXCL_LINE
      optimistic_write_lock_guard unlock_on_exit{*parent_lock};

      if (!unlikely(node_lock.try_upgrade_to_write_lock(version))) return {};
      unique_write_lock_obsoleting_guard obsolete_node_on_exit{node_lock};

      if (node_type == node_type::I4) {
        // TODO(laurynas): shorten the critical section by moving allocation
        // before it?
        auto current_node{make_db_inode_reclaimable_ptr(*this, node.node_4)};
        auto larger_node = detail::olc_inode_16::create(
            std::move(current_node), std::move(obsolete_node_on_exit),
            std::move(leaf), depth);
        *node_loc = larger_node.release();
        account_growing_inode<node_type::I16>();

      } else if (node_type == node_type::I16) {
        auto current_node{make_db_inode_reclaimable_ptr(*this, node.node_16)};
        auto larger_node = detail::olc_inode_48::create(
            std::move(current_node), std::move(obsolete_node_on_exit),
            std::move(leaf), depth);
        *node_loc = larger_node.release();
        account_growing_inode<node_type::I48>();

      } else {
        assert(node_type == node_type::I48);

        auto current_node{make_db_inode_reclaimable_ptr(*this, node.node_48)};
        auto larger_node = detail::olc_inode_256::create(
            std::move(current_node), std::move(obsolete_node_on_exit),
            std::move(leaf), depth);
        *node_loc = larger_node.release();
        account_growing_inode<node_type::I256>();
      }

      assert(!obsolete_node_on_exit.active());

      return true;
    }

    assert(child_loc != nullptr);

    const auto child = child_loc->load();

    if (unlikely(!node_lock.check(version))) return {};
    if (unlikely(!parent_lock->try_read_unlock(parent_version))) return {};

    parent_lock = &node_lock;
    parent_version = version;
    node = child;
    node_loc = child_loc;
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
  auto *parent_lock = &root_pointer_lock;
  auto optional_parent_version = parent_lock->try_read_lock();
  if (unlikely(!optional_parent_version)) return {};
  auto parent_version = *optional_parent_version;

  auto *node_loc{&root};
  auto node{root.load()};

  if (unlikely(!parent_lock->check(parent_version))) return {};

  if (unlikely(node == nullptr)) return false;

  auto *node_lock = &node_ptr_lock(node);
  auto optional_version = node_lock->try_read_lock();
  if (unlikely(!optional_version)) return {};
  auto version = *optional_version;

  auto node_type = node.type();

  if (unlikely(!node_lock->check(version))) return {};

  if (node_type == node_type::LEAF) {
    if (leaf::matches(node.leaf, k)) {
      if (unlikely(
              !root_pointer_lock.try_upgrade_to_write_lock(parent_version)))
        return {};  // LCOV_EXCL_LINE

      optimistic_write_lock_guard unlock_on_exit{root_pointer_lock};
      if (unlikely(!node_lock->try_upgrade_to_write_lock(version))) return {};

      node_lock->write_unlock_and_obsolete();

      const auto r{olc_art_policy::reclaim_leaf_on_scope_exit(node, *this)};
      root = nullptr;
      return true;
    }

    if (unlikely(!node_lock->try_read_unlock(version))) return {};

    return false;
  }

  detail::tree_depth depth{};
  auto remaining_key{k};

  while (true) {
    assert(node_type != node_type::LEAF);
    assert(depth < detail::art_key::size);

    const auto key_prefix_length = node.internal->key_prefix_length();
    const auto shared_prefix_length =
        node.internal->get_shared_key_prefix_length(remaining_key);

    if (shared_prefix_length < key_prefix_length) {
      if (unlikely(!parent_lock->try_read_unlock(parent_version))) return {};
      if (unlikely(!node_lock->try_read_unlock(version))) return {};

      return false;
    }

    if (unlikely(!node_lock->check(version))) return {};

    assert(shared_prefix_length == key_prefix_length);
    depth += key_prefix_length;
    remaining_key.shift_right(key_prefix_length);

    // FIXME(laurynas): child_loc, child_lock too similar
    auto [child_i, child_loc] =
        node.internal->find_child(node_type, remaining_key[0]);

    const auto is_node_min_size = node.internal->is_min_size();

    if (child_loc == nullptr) {
      if (unlikely(!parent_lock->try_read_unlock(parent_version))) return {};
      if (unlikely(!node_lock->try_read_unlock(version))) return {};

      return false;
    }

    auto child = child_loc->load();

    if (unlikely(!node_lock->check(version))) return {};

    auto &child_lock = node_ptr_lock(child);
    const auto optional_child_version = child_lock.try_read_lock();
    if (unlikely(!optional_child_version)) return {};
    const auto child_version = *optional_child_version;

    const auto child_type = child.type();

    if (child_type == node_type::LEAF) {
      if (!leaf::matches(child.leaf, k)) {
        if (unlikely(!parent_lock->try_read_unlock(parent_version))) return {};
        if (unlikely(!node_lock->try_read_unlock(version))) return {};
        if (unlikely(!child_lock.try_read_unlock(child_version))) return {};

        return false;
      }

      if (likely(!is_node_min_size)) {
        // TODO(laurynas): decrease_memory_use outside the critical section
        if (unlikely(!node_lock->try_upgrade_to_write_lock(version))) return {};
        optimistic_write_lock_guard unlock_on_exit{*node_lock};

        if (unlikely(!child_lock.try_upgrade_to_write_lock(child_version)))
          return {};  // LCOV_EXCL_LINE
        child_lock.write_unlock_and_obsolete();

        node.internal->remove(child_i, *this);

        return true;
      }

      assert(is_node_min_size);

      if (unlikely(!parent_lock->try_upgrade_to_write_lock(parent_version)))
        return {};  // LCOV_EXCL_LINE
      optimistic_write_lock_guard unlock_on_exit{*parent_lock};

      if (unlikely(!node_lock->try_upgrade_to_write_lock(version))) return {};
      unique_write_lock_obsoleting_guard obsolete_node_on_exit{*node_lock};

      if (unlikely(!child_lock.try_upgrade_to_write_lock(child_version))) {
        // LCOV_EXCL_START
        obsolete_node_on_exit.abort();
        return {};
        // LCOV_EXCL_STOP
      }
      unique_write_lock_obsoleting_guard obsolete_child_on_exit{child_lock};

      // TODO(laurynas): exception safety, OOM specifically
      if (node_type == node_type::I4) {
        obsolete_node_on_exit.commit();
        obsolete_child_on_exit.commit();
        auto current_node{make_db_inode_reclaimable_ptr(*this, node.node_4)};
        *node_loc = current_node->leave_last_child(child_i, *this);
        account_shrinking_inode<node_type::I4>();

      } else if (node_type == node_type::I16) {
        auto current_node{make_db_inode_reclaimable_ptr(*this, node.node_16)};
        auto new_node{detail::olc_inode_4::create(
            std::move(current_node), std::move(obsolete_node_on_exit), child_i,
            std::move(obsolete_child_on_exit))};
        *node_loc = new_node.release();
        account_shrinking_inode<node_type::I16>();

      } else if (node_type == node_type::I48) {
        auto current_node{make_db_inode_reclaimable_ptr(*this, node.node_48)};
        auto new_node{detail::olc_inode_16::create(
            std::move(current_node), std::move(obsolete_node_on_exit), child_i,
            std::move(obsolete_child_on_exit))};
        *node_loc = new_node.release();
        account_shrinking_inode<node_type::I48>();

      } else {
        assert(node_type == node_type::I256);

        auto current_node{make_db_inode_reclaimable_ptr(*this, node.node_256)};
        auto new_node{detail::olc_inode_48::create(
            std::move(current_node), std::move(obsolete_node_on_exit), child_i,
            std::move(obsolete_child_on_exit))};
        *node_loc = new_node.release();
        account_shrinking_inode<node_type::I256>();
      }

      assert(!obsolete_node_on_exit.active());
      assert(!obsolete_child_on_exit.active());

      return true;
    }

    assert(child_type != node_type::LEAF);

    parent_lock = node_lock;
    parent_version = version;

    node = child;
    node_loc = child_loc;
    node_lock = &child_lock;
    version = child_version;
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

  root = nullptr;
  current_memory_use.store(0, std::memory_order_relaxed);

  node_counts[as_i<node_type::I4>].store(0, std::memory_order_relaxed);
  node_counts[as_i<node_type::I16>].store(0, std::memory_order_relaxed);
  node_counts[as_i<node_type::I48>].store(0, std::memory_order_relaxed);
  node_counts[as_i<node_type::I256>].store(0, std::memory_order_relaxed);
}

#if defined(__GNUC__) && (__GNUC__ >= 9)
DISABLE_GCC_WARNING("-Wsuggest-attribute=cold")
#endif

void olc_db::increase_memory_use(std::size_t delta) {
  if (delta == 0) return;

  current_memory_use.fetch_add(delta, std::memory_order_relaxed);
}

#if defined(__GNUC__) && (__GNUC__ >= 9)
RESTORE_GCC_WARNINGS()
#endif

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
