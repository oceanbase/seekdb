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
  for (auto i = last - first - 1; i > 0; --i) {
    std::swap(first[i], first[std::rand() % (i + 1)]);
  }
}
}

#endif // __ANDROID__ && __cplusplus
#endif // SEEKDB_ANDROID_COMPAT_H
