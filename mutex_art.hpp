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

#include <mutex>
#include <type_traits>
#include <utility>

#include "art.hpp"
#include "node_type.hpp"

namespace unodb {

class mutex_db final {
 public:
  using value_view = unodb::value_view;

  // If the search key was found, that is, the first pair member has a value,
  // then the second member is a locked tree mutex which must be released ASAP
  // after reading the first pair member. Otherwise, the second member is
  // undefined.
  using get_result = std::pair<db::get_result, std::unique_lock<std::mutex>>;

 protected:
  // Querying
  [[nodiscard]] auto get0(const detail::art_key k) const {
    std::unique_lock guard{mutex};
    const auto db_get_result{db_.get0(k)};
    if (!db_get_result) {
      guard.unlock();
      return std::make_pair(db_get_result, std::unique_lock<std::mutex>{});
    }
    return std::make_pair(db_get_result, std::move(guard));
  }

  // Modifying
  // Cannot be called during stack unwinding with std::uncaught_exceptions() > 0
  [[nodiscard]] auto insert0(const detail::art_key k, value_view v) {
    const std::lock_guard guard{mutex};
    return db_.insert0(k, v);
  }

  [[nodiscard]] auto remove0(const detail::art_key k) {
    const std::lock_guard guard{mutex};
    return db_.remove0(k);
  }

 public:
  // Creation and destruction
  mutex_db() noexcept = default;

  // Query for a value associated with a binary comparable key.
  [[nodiscard, gnu::pure]] get_result get(key_view search_key) const noexcept {
    detail::art_key k{search_key};
    return get0(k);
  }

  // Querying for a value associated with an external key.  The key is
  // converted to a binary comparable key.
  [[nodiscard, gnu::pure]]
  typename std::enable_if<std::is_integral<key>::value, get_result>::type
  get(key search_key) const noexcept {
    const detail::art_key k{search_key};  // fast path conversion.
    return get0(k);
  }

  [[nodiscard]] auto empty() const {
    const std::lock_guard guard{mutex};
    return db_.empty();
  }

  // Insert a value under a binary comparable key iff there is no
  // entry for that key.
  //
  // Note: Cannot be called during stack unwinding with
  // std::uncaught_exceptions() > 0
  //
  // @return true iff the key value pair was inserted.
  [[nodiscard, gnu::pure]] bool insert(key_view insert_key, value_view v) {
    detail::art_key k{insert_key};
    return insert0(k, v);
  }

  // Insert a value under the key.  The key is converted to a binary
  // comparable key.
  //
  // Note: Cannot be called during stack unwinding with
  // std::uncaught_exceptions() > 0
  //
  // @return true iff the key value pair was inserted.
  [[nodiscard, gnu::pure]]
  typename std::enable_if<std::is_integral<key>::value, bool>::type
  insert(key insert_key, value_view v) {
    const detail::art_key k{insert_key};  // fast path conversion.
    return insert0(k, v);
  }

  // Remove the entry associated with the binary comparable key.
  //
  // @return true if the delete was successful (i.e. the key was found
  // in the tree and the associated index entry was removed).
  [[nodiscard, gnu::pure]] bool remove(key_view search_key) {
    detail::art_key k{search_key};
    return remove0(k);
  }

  // Remove the entry associated with the key.
  //
  // @return true if the delete was successful (i.e. the key was found
  // in the tree and the associated index entry was removed).
  [[nodiscard, gnu::pure]]
  typename std::enable_if<std::is_integral<key>::value, bool>::type
  remove(key search_key) {
    const detail::art_key k{search_key};  // fast path conversion.
    return remove0(k);
  }

  // Removes all entries in the index.
  void clear() {
    const std::lock_guard guard{mutex};
    db_.clear();
  }

  //
  // scan API.
  //

  using iterator = unodb::db::iterator;

  // Scan the tree, applying the caller's lambda to each visited leaf.
  // The tree remains locked for the duration of the scan.
  //
  // @param fn A function f(unodb::visitor<unodb::mutex_db::iterator>&)
  // returning [bool::halt].  The traversal will halt if the function
  // returns [true].
  //
  // @param fwd When [true] perform a forward scan, otherwise perform
  // a reverse scan.
  template <typename FN>
  inline void scan(FN fn, bool fwd = true) noexcept {
    const std::lock_guard guard{mutex};
    db_.scan(fn, fwd);
  }

  // Scan in the indicated direction, applying the caller's lambda to
  // each visited leaf.  The tree remains locked for the duration of
  // the scan.
  //
  // @param fromKey is an inclusive lower bound for the starting point
  // of the scan.
  //
  // @param fn A function f(unodb::visitor<unodb::mutex_db::iterator>&)
  // returning [bool::halt].  The traversal will halt if the function
  // returns [true].
  //
  // @param fwd When [true] perform a forward scan, otherwise perform
  // a reverse scan.
  template <typename FN>
  inline void scan_from(const key fromKey, FN fn, bool fwd = true) noexcept {
    const std::lock_guard guard{mutex};
    db_.scan_from(fromKey, fn, fwd);
  }

  // Scan a half-open key range, applying the caller's lambda to each
  // visited leaf.  The tree remains locked for the duration of the
  // scan.  The scan will proceed in lexicographic order iff fromKey
  // is less than toKey and in reverse lexicographic order iff toKey
  // is less than fromKey.  When fromKey < toKey, the scan will visit
  // all index entries in the half-open range [fromKey,toKey) in
  // forward order.  Otherwise the scan will visit all index entries
  // in the half-open range (fromKey,toKey] in reverse order.
  //
  // @param fromKey is an inclusive bound for the starting point of
  // the scan.
  //
  // @param toKey is an exclusive bound for the ending point of the
  // scan.
  //
  // @param fn A function f(unodb::visitor<unodb::mutex_db::iterator>&)
  // returning [bool::halt].  The traversal will halt if the function
  // returns [true].
  template <typename FN>
  inline void scan_range(const key fromKey, const key toKey, FN fn) noexcept {
    const std::lock_guard guard{mutex};
    db_.scan_range(fromKey, toKey, fn);
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
    return db_.get_node_count<NodeType>();
  }

  [[nodiscard]] auto get_node_counts() const {
    const std::lock_guard guard{mutex};
    return db_.get_node_counts();
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_growing_inode_count() const {
    const std::lock_guard guard{mutex};
    return db_.get_growing_inode_count<NodeType>();
  }

  [[nodiscard]] auto get_growing_inode_counts() const {
    const std::lock_guard guard{mutex};
    return db_.get_growing_inode_counts();
  }

  template <node_type NodeType>
  [[nodiscard]] auto get_shrinking_inode_count() const {
    const std::lock_guard guard{mutex};
    return db_.get_shrinking_inode_count<NodeType>();
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
  db db_;
  mutable std::mutex mutex;
};

}  // namespace unodb

#endif  // UNODB_DETAIL_MUTEX_ART_HPP
