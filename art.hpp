#ifndef UNODB_ART_HPP
#define UNODB_ART_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace unodb {

using key_type = uint64_t;
using value_type = std::vector<std::byte>;

class db {
public:
    db() noexcept { }
    void insert(key_type k, value_type t);
};

}

#endif
