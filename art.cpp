// Copyright 2019 Laurynas Biveinis
#include "art.hpp"

namespace {

[[nodiscard]] auto make_binary_comparable(const uint64_t key)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(key);
#else
#error Needs implementing
#endif
}

} // namespace

namespace unodb {

void db::insert(key_type k, value_type t)
{
}

} // namespace unodb
