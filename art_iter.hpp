// Copyright 2019-2024 Laurynas Biveinis
#ifndef UNODB_DETAIL_ART_ITER_HPP
#define UNODB_DETAIL_ART_ITER_HPP

//
// ART Iterator Implementation
//

namespace unodb {

inline std::optional<const key> db::iterator::get_key() noexcept {
  // FIXME Eventually this will need to use the stack to reconstruct
  // the key from the path from the root to this leaf.  Right now it
  // is relying on the fact that simple fixed width keys are stored
  // directly in the leaves.
  if ( ! valid() ) return {}; // not positioned on anything.
  const auto& e = stack_.top();
  const auto& node = std::get<NP>( e );
  UNODB_DETAIL_ASSERT( node.type() == node_type::LEAF ); // On a leaf.
  const auto *const leaf{ node.ptr<detail::leaf *>() }; // current leaf.
  key_ = leaf->get_key().decode(); // decode the key into the iterator's buffer.
  return key_; // return pointer to the internal key buffer.
}

inline std::optional<const value_view> db::iterator::get_val() const noexcept {
  if ( ! valid() ) return {}; // not positioned on anything.
  const auto& e = stack_.top();
  const auto& node = std::get<NP>( e );
  UNODB_DETAIL_ASSERT( node.type() == node_type::LEAF ); // On a leaf.
  const auto *const leaf{ node.ptr<detail::leaf *>() }; // current leaf.
  return leaf->get_value_view();
}

inline bool db::iterator::operator==(const iterator& other) const noexcept {
  if ( &db_ != &other.db_ ) return false;                     // different tree?
  if ( stack_.empty() != other.stack_.empty() ) return false; // one stack is empty and the other is not?
  if ( stack_.empty() ) return true;                          // both empty.
  // TODO Is this any different for OLC where there could be two
  // different tree structures and hence two iterators that point
  // at the same (key,val) in a leaf but there is a different
  // inode path?  In that case, this would say that the iterators
  // are not the same.  Which seems to be the correct answer. (The
  // main reason to compare iterators is to detect the end().)
  const auto& a = stack_.top();
  const auto& b = other.stack_.top();
  return a == b; // top of stack is same (inode, key, and child_index).
}

inline bool db::iterator::operator!=(const iterator& other) const noexcept { return !(*this == other); }

template <typename FN>
inline void db::scan(FN fn, bool fwd) noexcept {
  if ( empty() ) return;
  if ( fwd ) {
    auto it { iterator(*this).first() };
    visitor v{ it };
    while ( it.valid() ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      it.next();
    }
  } else {
    auto it { iterator(*this).last() };
    visitor v { it };
    while ( it.valid() ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      it.prior();
    }
  }
}

template <typename FN>
inline void db::scan_from(const key fromKey_, FN fn, bool fwd) noexcept {
  if ( empty() ) return;
  const detail::art_key fromKey{fromKey_};  // convert to internal key
  bool match {};
  if ( fwd ) {
    auto it { iterator(*this).seek( fromKey, match, true/*fwd*/ ) };
    visitor v { it };
    while ( it.valid() ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      it.next();
    }
  } else {
    auto it { iterator(*this).seek( fromKey, match, false/*fwd*/ ) };
    visitor v { it };
    while ( it.valid() ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      it.prior();
    }
  }
}

// FIXME There should be a cheaper way to handle the exclusive bound
// case.  This relies on key decoding, which is expensive for variable
// length keys.  At a minimum, we could compare the internal keys to
// avoid the decoding.  But it would be nice to know the leaf that we
// will not visit and just halt when we get there.
template <typename FN>
inline void db::scan_range(const key fromKey_, const key toKey_, FN fn) noexcept {
  constexpr bool debug = false;  // set true to debug scan. FIXME REMOVE [debug]?
  if ( empty() ) return;
  const detail::art_key fromKey{fromKey_};  // convert to internal key
  const detail::art_key toKey{toKey_};      // convert to internal key
  const auto ret = fromKey.cmp( toKey );    // compare the internal keys.
  const bool fwd { ret < 0 };               // fromKey is less than toKey
  if ( ret == 0 ) return;                   // NOP if fromKey == toKey since toKey is exclusive upper bound.
  bool match {};
  if ( fwd ) {
    auto it1 { iterator(*this).seek( fromKey, match, true/*fwd*/ ) }; // lower bound
    // auto it2 { end().seek( toKey, match, true/*fwd*/ ) }; // upper bound
    // if ( it2.get_key() == toKey_ ) it2.prior();  // back up one if the toKey exists (exclusive upper bound).
    if constexpr ( debug ) {
      std::cerr<<"scan:: fwd"<<std::endl;
      std::cerr<<"scan:: fromKey="<<fromKey_<<std::endl; it1.dump(std::cerr);
      // std::cerr<<"scan:: toKey="<<toKey_<<std::endl; it2.dump(std::cerr);
    }
    visitor v { it1 };
    while ( it1.valid() && it1.get_key() < toKey_ ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      // if ( UNODB_DETAIL_UNLIKELY( it1.current_node() == it2.current_node() ) ) break;
      it1.next();
      if constexpr( debug ) {
        std::cerr<<"scan: next()"<<std::endl; it1.dump( std::cerr );
      }
    }
  } else { // reverse traversal.
    auto it1 { iterator(*this).seek( fromKey, match, true/*fwd*/ ) }; // upper bound
    // auto it2 { end().seek( toKey, match, false/*fwd*/ ) }; // lower bound
    // if ( it2.get_key() == toKey_ ) it2.next();  // advance one if the toKey exists (exclusive lower bound during reverse traversal)
    if constexpr( debug ) {
      std::cerr<<"scan:: rev"<<std::endl;
      std::cerr<<"scan:: fromKe   y="<<fromKey_<<std::endl; it1.dump(std::cerr);
    // std::cerr<<"scan:: toKey="<<toKey_<<std::endl; it2.dump(std::cerr);
    }
    visitor v { it1 };
    while ( it1.valid() && it1.get_key() > toKey_ ) {
      if ( UNODB_DETAIL_UNLIKELY( fn( v ) ) ) break;
      // if ( UNODB_DETAIL_UNLIKELY( it1.current_node() == it2.current_node() ) ) break;
      it1.prior();
      if constexpr( debug ) {
      std::cerr<<"scan: prior()"<<std::endl; it1.dump( std::cerr );
      }
    }
  }
}

} // namespace unodb

#endif // UNODB_DETAIL_ART_ITER_HPP
