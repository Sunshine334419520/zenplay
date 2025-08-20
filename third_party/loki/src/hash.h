/**
* @Author: YangGuang
* @Date:   2018-10-10
* @Email:  guang334419520@126.com
* @Filename: hash.h
* @Last modified by:  YangGuang
*/
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <string>
#include <utility>

#include "loki_export.h"

namespace loki {

LOKI_EXPORT uint32_t Hash(const void* data, size_t length);
LOKI_EXPORT uint32_t Hash(const std::string& str);
// LOKI_EXPORT uint32_t Hash(cosnt string16& str);

LOKI_EXPORT uint32_t PersistentHash(const void* data, size_t length);
LOKI_EXPORT uint32_t PersistentHash(const std::string& str);

// Hash pairs of 32-bit or 64-bit numbers.
LOKI_EXPORT size_t HashInts32(uint32_t value1, uint32_t value2);
LOKI_EXPORT size_t HashInts64(uint64_t value1, uint64_t value2);

template <typename T1, typename T2>
inline size_t HashInts(T1 value1, T2 value2) {
  // This condition is expected to be compile-time evaluated and optimised away
  // in release builds.
  if (sizeof(T1) > sizeof(uint32_t) || (sizeof(T2) > sizeof(uint32_t)))
    return HashInts64(value1, value2);

  return HashInts32(value1, value2);
}

// A templated hasher for pairs of integer types. Example:
//
//   using MyPair = std::pair<int32_t, int32_t>;
//   std::unordered_set<MyPair, loki::IntPairHash<MyPair>> set;
template <typename T>
struct IntPairHash;

template <typename Type1, typename Type2>
struct IntPairHash<std::pair<Type1, Type2>> {
  size_t operator()(std::pair<Type1, Type2> value) const {
    return HashInts(value.first, value.second);
  }
};

}  // namespace loki.
