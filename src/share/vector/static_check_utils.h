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

#ifndef OCEANBASE_SHARE_VECTOR_STATIC_CHECK_UTILS_H_
#define OCEANBASE_SHARE_VECTOR_STATIC_CHECK_UTILS_H_

// define TYPE_CHECK
// eg:  TYPE_CHECK_DEF(is_fixed_type, int8_t, int16_t, int32_t, int64_t,
//                     uint8_t, uint16_t, uint32_t, uint64_t,
//                     double, float);
//
// static_assert(is_fixed_type<T>::value, "invalid type");

template <typename T, typename... Args>
struct exist_type {
  static const bool value = false;
};

template <typename T, typename First, typename... Rest>
struct exist_type<T, First, Rest...> {
  static const bool value = std::is_same<T, First>::value || exist_type<T, Rest...>::value;
};

// #define TYPE_CHECKER_DEF(checker_name, ...)                          \
// template <typename T>                                                \
// struct checker_name {                                                \
//   static constexpr bool value = exist_type<T, ##__VA_ARGS__>::value; \
// };

// define VALUE_CHECK
// eg: DEF_CHECK_VALUE(VecValueTypeClass, is_decimal_tc,
//                     VEC_TC_DECIMAL_INT32,  VEC_TC_DECIMAL_INT64,
//                     VEC_TC_DECIMAL_INT128,  VEC_TC_DECIMAL_INT256,
//                     VEC_TC_DECIMAL_INT512);
//
// if (is_decimal_int<T>::value) { ... }
//

template <typename T, T v, T... args>
struct exist_value {
 static constexpr bool value = false;
};

template <typename T, T v, T First, T... Rest>
struct exist_value<T, v, First, Rest...> {
 static constexpr bool value = (v == First) || exist_value<T, v, Rest...>::value;
};

#define VALUE_CHECKER_DEF(type, checker_name, ...)                         \
template <type v>                                                          \
struct checker_name {                                                      \
    static constexpr bool value = exist_value<type, v, ##__VA_ARGS__>;     \
}; \                                                                       \

#endif // OCEANBASE_SHARE_VECTOR_STATIC_CHECK_UTILS_H_
