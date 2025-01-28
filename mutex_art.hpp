// Copyright 2019-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_MUTEX_ART_HPP
#define UNODB_DETAIL_MUTEX_ART_HPP

//
// CAUTION: [global.hpp] MUST BE THE FIRST INCLUDE IN ALL SOURCE AND
// HEADER FILES !!!
//
// This header defines _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC for
// DEBUG builds.  If some standard headers are included before and
// after those symbols are defined, then that results in different
// container internal structure layouts and that is Not Good.
#include "global.hpp"  // IWYU pragma: keep

#include <cassert>
#include <mutex>
#include <type_traits>
#include <utility>

#include "art.hpp"
#include "assert.hpp"
#include "node_type.hpp"

namespace unodb {

/// A thread-safe implementation of the Adaptive Radix Tree (ART)
/// using an explicit global lock.  All get, insert, remove and scan
/// operations take the global lock and hold it for the duration of
/// the operation.
///
/// See olc_art for a highly concurrent thread-safe ART
/// implementation.
template <typename Key>
class mutex_db final {
 public:
  using key_type = Key;
  using value_view = unodb::value_view;

  // If the search key was found, that is, the first pair member has a value,
  // then the second member is a locked tree mutex which must be released ASAP
  // after reading the first pair member. Otherwise, the second member is
  // undefined.
  using get_result =
      std::pair<typename db<Key>::get_result, std::unique_lock<std::mutex>>;

 private:
  using art_key_type = detail::basic_art_key<Key>;

  /// Querying with an encoded key.
  [[nodiscard]] auto get_internal(art_key_type k) const {
    std::unique_lock guard{mutex};
    const auto db_get_result{db_.get_internal(k)};
    if (!db_get_result) {
      guard.unlock();
      return std::make_pair(db_get_result, std::unique_lock<std::mutex>{});
    }
    return std::make_pair(db_get_result, std::move(guard));
  }

  /// Modifying with an encoded key.
  ///
  /// Cannot be called during stack unwinding with
  /// std::uncaught_exceptions() > 0
  [[nodiscard]] auto insert_internal(art_key_type k, value_view v) {
    const std::lock_guard guard{mutex};
    return db_.insert_internal(k, v);
  }

  /// Removing with an encoded key.
  [[nodiscard]] auto remove_internal(art_key_type k) {
    const std::lock_guard guard{mutex};
    return db_.remove_internal(k);
  }

 public:
  // Creation and destruction
  mutex_db() noexcept = default;

  /// Query for a value associated with a key.
  ///
  /// @param search_key If Key is a simple primitive type, then it is
  /// converted into a binary comparable key.  If Key is key_value,
  /// then it is assumed to already be a binary comparable key, e.g.,
  /// as produced by unodb::key_encoder.
  [[nodiscard, gnu::pure]] get_result get(Key search_key) const noexcept {
    art_key_type k{search_key};
    return get_internal(k);
  }

  [[nodiscard]] auto empty() const {
    const std::lock_guard guard{mutex};
    return db_.empty();
  }

  /// Insert a value under a key iff there is no entry for that key.
  ///
  /// Note: Cannot be called during stack unwinding with
  /// std::uncaught_exceptions() > 0
  ///
  /// @param insert_key If Key is a simple primitive type, then it is
  /// converted into a binary comparable key.  If Key is key_value,
  /// then it is assumed to already be a binary comparable key, e.g.,
  /// as produced by unodb::key_encoder.
  ///
  /// @return true iff the key value pair was inserted.
  [[nodiscard]] bool insert(Key insert_key, value_view v) {
    const art_key_type k{insert_key};
    return insert_internal(k, v);
  }

  /// Remove the entry associated with the key.
  ///
  /// @param search_key If Key is a simple primitive type, then it is
  /// converted into a binary comparable key.  If Key is key_value,
  /// then it is assumed to already be a binary comparable key, e.g.,
  /// as produced by unodb::key_encoder.
  [[nodiscard]] bool remove(Key search_key) {
    const auto k = art_key_type{search_key};
    return remove_internal(k);
  }

  // Removes all entries in the index.
  void clear() {
    const std::lock_guard guard{mutex};
    db_.clear();
  }

  //
  // scan API.
  //

  using iterator = typename unodb::db<Key>::iterator;

  /// Scan the tree, applying the caller's lambda to each visited
  /// leaf.  The tree remains locked for the duration of the scan.
  ///
  /// @param fn A function
  /// f(unodb::visitor<unodb::mutex_db::iterator>&) returning
  /// [bool::halt].  The traversal will halt if the function returns
  /// [true].
  ///
  /// @param fwd When [true] perform a forward scan, otherwise perform
  /// a reverse scan.
  template <typename FN>
  void scan(FN fn, bool fwd = true) noexcept {
    const std::lock_guard guard{mutex};
    db_.scan(fn, fwd);
  }

  /// Scan in the indicated direction, applying the caller's lambda to
  /// each visited leaf.  The tree remains locked for the duration of
  /// the scan.
  ///
  /// @param from_key is an inclusive lower bound for the starting
  /// point of the scan.
  ///
  /// @param fn A function
  /// f(unodb::visitor<unodb::mutex_db::iterator>&) returning
  /// [bool::halt].  The traversal will halt if the function returns
  /// [true].
  ///
  /// @param fwd When [true] perform a forward scan, otherwise perform
  /// a reverse scan.
  template <typename FN>
  void scan_from(Key from_key, FN fn, bool fwd = true) noexcept {
    const std::lock_guard guard{mutex};
    db_.scan_from(from_key, fn, fwd);
  }

  /// Scan a half-open key range, applying the caller's lambda to each
  /// visited leaf.  The tree remains locked for the duration of the
  /// scan.  The scan will proceed in lexicographic order iff fromKey
  /// is less than toKey and in reverse lexicographic order iff toKey
  /// is less than fromKey.  When fromKey < toKey, the scan will visit
  /// all index entries in the half-open range [fromKey,toKey) in
  /// forward order.  Otherwise the scan will visit all index entries
  /// in the half-open range (fromKey,toKey] in reverse order.
  ///
  /// @param from_key is an inclusive bound for the starting point of
  /// the scan.
  ///
  /// @param to_key is an exclusive bound for the ending point of the
  /// scan.
  ///
  /// @param fn A function
  /// f(unodb::visitor<unodb::mutex_db::iterator>&) returning
  /// [bool::halt].  The traversal will halt if the function returns
  /// [true].
  template <typename FN>
  void scan_range(Key from_key, Key to_key, FN fn) noexcept {
    const std::lock_guard guard{mutex};
    db_.scan_range(from_key, to_key, fn);
  }

  //
  // TEST ONLY METHODS
  //

  // Used to write the iterator tests.
  auto test_only_iterator() noexcept { return db_.test_only_iterator(); }

  // Stats
#ifdef UNODB_DETAIL_WITH_STATS

  [[nodiscard]] auto get_current_memory_use() const {
    const std::lock_guard guard{mutex};
    return db_.get_current_memory_use();
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_node_count() const {
    const std::lock_guard guard{mutex};
    return db_.template get_node_count<NodeType>();
  }

  [[nodiscard]] auto get_node_counts() const {
    const std::lock_guard guard{mutex};
    return db_.get_node_counts();
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_growing_inode_count() const {
    const std::lock_guard guard{mutex};
    return db_.template get_growing_inode_count<NodeType>();
  }

  [[nodiscard]] auto get_growing_inode_counts() const {
    const std::lock_guard guard{mutex};
    return db_.get_growing_inode_counts();
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_shrinking_inode_count() const {
    const std::lock_guard guard{mutex};
    return db_.template get_shrinking_inode_count<NodeType>();
  }

  [[nodiscard]] auto get_shrinking_inode_counts() const {
    const std::lock_guard guard{mutex};
    return db_.get_shrinking_inode_counts();
  }

  [[nodiscard]] auto get_key_prefix_splits() const {
    const std::lock_guard guard{mutex};
    return db_.get_key_prefix_splits();
  }

#endif  // UNODB_DETAIL_WITH_STATS

  // Public utils

  // Releases the mutex in the case key was not found, keeps it locked
  // otherwise.
  [[nodiscard]] static auto key_found(const get_result &result) noexcept {
#ifndef NDEBUG
    const auto &lock{result.second};
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    assert(!result.first || lock.owns_lock());
#endif

    return static_cast<bool>(result.first);
  }

  // Debugging
  [[gnu::cold]] UNODB_DETAIL_NOINLINE void dump(std::ostream &os) const {
    const std::lock_guard guard{mutex};
    db_.dump(os);
  }

 private:
  db<Key> db_;
  mutable std::mutex mutex;
};

}  // namespace unodb

#endif  // UNODB_DETAIL_MUTEX_ART_HPP
