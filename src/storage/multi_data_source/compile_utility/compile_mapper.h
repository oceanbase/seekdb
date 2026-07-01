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

#ifndef SHARE_STORAGE_MULTI_DATA_SOURCE_COMPILE_MAPPER_H
#define SHARE_STORAGE_MULTI_DATA_SOURCE_COMPILE_MAPPER_H
#define NEED_MDS_REGISTER_DEFINE
#include "mds_register.h"
#undef NEED_MDS_REGISTER_DEFINE
#include "map_type_index_in_tuple.h"
#include "deps/oblib/src/common/meta_programming/ob_type_traits.h"
#include "deps/oblib/src/common/meta_programming/ob_meta_compare.h"
#include "deps/oblib/src/common/meta_programming/ob_meta_copy.h"
// This file is responsible for generating two compile-time mapping relationships, one from the Helper type and BufferCtx type to ID, as well as the reverse mapping
// Another is the mapping from Data type to multi-version flags, no reverse mapping needed
namespace oceanbase
{
namespace storage
{
namespace mds
{

template <typename T>
class __TypeMapper {};

template <int ID>
class __IDMapper {};
// Call the following macro to generate compile-time TYPE <-> ID mapping relationship
#define REGISTER_TYPE_ID(HELPER, CTX, ID) \
template <>\
class __TypeMapper<HELPER> {\
public:\
  static const std::uint64_t id = ID;\
};\
template <>\
class __IDMapper<ID> {\
public:\
  typedef HELPER helper_type;\
  typedef CTX ctx_type;\
};

#define NEED_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION
#define _GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION_(helper_type, buffer_ctx_type, ID, TEST) \
REGISTER_TYPE_ID(helper_type, buffer_ctx_type, ID)
#include "mds_register.h"
#undef _GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION_
#undef NEED_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION
// Obtain this information at compile time through the following macros
template <typename Tuple, int IDX>
struct TupleIdxType {
  typedef typename std::decay<decltype(std::declval<Tuple>().template element<IDX>())>::type type;
};
template <typename Tuple, typename Type>
struct TupleTypeIdx {
  template <int IDX>
  static constexpr int64_t get_type_idx() {
    return std::is_same<Type, typename TupleIdxType<Tuple, IDX>::type>::value ?
                              IDX :
                              get_type_idx<IDX + 1>();
  }
  template <>
  static constexpr int64_t get_type_idx<Tuple::get_element_size()>() {
    return Tuple::get_element_size();
  }
  static constexpr int64_t value = get_type_idx<0>();
};
#define GET_HELPER_TYPE_BY_ID(ID) __IDMapper<ID>::helper_type
#define GET_HELPER_ID_BY_TYPE(T) __TypeMapper<T>::id
#define GET_CTX_TYPE_BY_ID(ID) __IDMapper<ID>::ctx_type
#define GET_CTX_TYPE_BY_TUPLE_IDX(IDX) \
typename std::decay<decltype(std::declval<BufferCtxTupleHelper>().element<IDX>())>::type

#define GET_MULTIVERSION_FLAG(K, V) get_multi_version_flag<K, V>()

#undef REGISTER_TYPE_ID
#undef REGISTER_TYPE_MULTI_VERSION_FLAG

}
}
}

#endif
