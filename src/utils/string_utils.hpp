// Copyright (c) 2016 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef __Octopus__string_utils__
#define __Octopus__string_utils__

#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <cstddef>
#include <type_traits>
#include <iomanip>
#include <cctype>

#include <boost/algorithm/string/join.hpp>

namespace octopus { namespace utils {

template <typename T>
std::vector<std::string> split(const T& str, const char delim) {
    std::vector<std::string> elems;
    elems.reserve(std::count(std::cbegin(str), std::cend(str), delim) + 1);
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.emplace_back(item);
    }
    return elems;
}

inline std::string join(const std::vector<std::string>& strings, const std::string delim)
{
    return boost::algorithm::join(strings, delim);
}

template <typename T>
bool is_prefix(const T& lhs, const T& rhs)
{
    return std::equal(std::cbegin(lhs), std::cend(lhs), std::cbegin(rhs));
}

template <typename T>
bool is_suffix(const T& lhs, const T& rhs)
{
    return std::equal(std::cbegin(lhs), std::cend(lhs), std::next(std::cbegin(rhs)));
}

inline std::size_t length(const char* str)
{
    return std::strlen(str);
}

inline std::size_t length(const std::string& str)
{
    return str.length();
}

inline bool find(const std::string& lhs, const std::string& rhs)
{
    return lhs.find(rhs) != std::string::npos;
}

template <typename T, typename = typename std::enable_if_t<std::is_floating_point<T>::value>>
std::string to_string(const T val, const unsigned precision = 2)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << val;
    return out.str();
}

template <typename T>
std::vector<std::string> to_strings(const std::vector<T>& values)
{
    std::vector<std::string> result {};
    result.reserve(values.size());
    std::transform(std::cbegin(values), std::cend(values), std::back_inserter(result),
                   [] (auto value) { return std::to_string(value); });
    return result;
}

template <typename T, typename = typename std::enable_if_t<std::is_floating_point<T>::value>>
std::vector<std::string> to_strings(const std::vector<T>& values, const unsigned precision = 2)
{
    std::vector<std::string> result {};
    result.reserve(values.size());
    std::transform(std::cbegin(values), std::cend(values), std::back_inserter(result),
                   [precision] (auto value) { return to_string(value, precision); });
    return result;
}

inline std::string& capitalise_front(std::string& str) noexcept
{
    if (!str.empty()) str.front() = std::toupper(str.front());
    return str;
}

inline std::string capitalise_front(const std::string& str)
{
    auto result = str;
    return capitalise_front(result);
}

} // namespace utils
} // namespace octopus

#endif /* defined(__Octopus__string_utils__) */