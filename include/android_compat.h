/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Android NDK portability fixes for test code.
// Force-included via -include in CMakeLists.txt when ANDROID is set.
#ifndef SEEKDB_ANDROID_COMPAT_H
#define SEEKDB_ANDROID_COMPAT_H

#if defined(__ANDROID__) && defined(__cplusplus)

// std::random_shuffle was removed in C++17; provide a simple replacement.
#include <algorithm>
#include <cstdlib>
namespace std {
template <class RandomIt>
inline void random_shuffle(RandomIt first, RandomIt last) {
  for (typename std::iterator_traits<RandomIt>::difference_type i = last - first - 1; i > 0; --i) {
    std::swap(first[i], first[std::rand() % (i + 1)]);
  }
}
}

#endif // __ANDROID__ && __cplusplus
#endif // SEEKDB_ANDROID_COMPAT_H
