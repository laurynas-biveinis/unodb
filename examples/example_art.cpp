// Copyright 2024 Laurynas Biveinis

// A simple CRUD example for unodb::db. For simplicity &
// self-containedness does not concern with exception handling and refactoring
// the duplicated code with other examples.

// IWYU pragma: no_include <__ostream/basic_ostream.h>
// IWYU pragma: no_include <gsl/span>

#include "global.hpp"  // IWYU pragma: keep

#include <cstddef>
#include <iostream>
#include <string_view>

#include "art.hpp"

namespace {

constexpr std::string_view value_1 = "Value 1";
constexpr std::string_view value_2 = "Another value";
constexpr std::string_view value_3 = "A third value";

[[nodiscard, gnu::pure]] unodb::value_view from_string_view(
    std::string_view sv) {
  return {reinterpret_cast<const std::byte*>(sv.data()), sv.length()};
}

}  // namespace

int main() {
  unodb::db tree;
  std::cerr << "The tree starts out as empty: " << tree.empty() << '\n';

  auto insert_result = tree.insert(1, from_string_view(value_1));
  std::cerr << "Insert key 1 result: " << insert_result << '\n';

  std::cerr << "The tree is not empty anymore: " << tree.empty() << '\n';

  insert_result = tree.insert(10, from_string_view(value_2));
  std::cerr << "Insert key 10 result: " << insert_result << '\n';

  insert_result = tree.insert(50, from_string_view(value_3));
  std::cerr << "Insert key 50 result: " << insert_result << '\n';

  // visitor for scans.
  auto fn = [](const unodb::visitor<typename unodb::db::iterator>& v) {
    const auto& val = v.get_value();
    const std::string_view s(reinterpret_cast<const char*>(val.data()),
                             val.size());
    std::cerr << "{key=" << v.get_key() << ",val=\"" << s << "\""
              << "} ";
    return false;  // do not halt
  };

  // full forward scan
  std::cerr << "forward scan:: ";
  tree.scan(fn);
  std::cerr << "\n";

  // full reverse scan
  std::cerr << "reverse scan:: ";
  tree.scan(fn, false);
  std::cerr << "\n";

  // forward range scan
  std::cerr << "forward half-open key-range scan [10,50):: ";
  tree.scan_range(10, 50, fn);
  std::cerr << "\n";

  // reverse range scan.
  std::cerr << "reverse half-open key-range scan (50,10]:: ";
  tree.scan_range(50, 10, fn);
  std::cerr << "\n";

  auto get_result = tree.get(20);
  std::cerr << "Get key 20 result has value: " << get_result.has_value()
            << '\n';

  get_result = tree.get(10);
  std::cerr << "Get key 10 result has value: "
            // Alternative to get_result.has_value
            << unodb::db::key_found(get_result)
            << ", value length: " << get_result->size() << '\n';

  auto remove_result = tree.remove(20);
  std::cerr << "Remove key 20 result: " << remove_result << '\n';

  remove_result = tree.remove(10);
  std::cerr << "Remove key 10 result: " << remove_result << '\n';
  get_result = tree.get(10);
  std::cerr << "Get key 10 result has value: " << get_result.has_value()
            << '\n';

  tree.clear();
}
