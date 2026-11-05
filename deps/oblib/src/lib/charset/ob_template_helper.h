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

#ifndef OB_TEMPLATE_HELPER_H
#define OB_TEMPLATE_HELPER_H

#include <stddef.h>


template< size_t N >
constexpr size_t str_length( char const (&)[N] )
{
  return N-1;
}

template <class T, size_t N>
constexpr size_t array_elements(T (&)[N]) noexcept {
  return N;
}

/**
  Casts from one pointer type, to another, without using
  reinterpret_cast or C-style cast:
    foo *f; bar *b= pointer_cast<bar*>(f);
  This avoids having to do:
    foo *f; bar *b= static_cast<bar*>(static_cast<void*>(f));
 */
template <typename T>
inline T pointer_cast(void *p) {
  return static_cast<T>(p);
}

template <typename T>
inline const T pointer_cast(const void *p) {
  return static_cast<T>(p);
}

#endif // OB_TEMPLATE_HELPER_H
