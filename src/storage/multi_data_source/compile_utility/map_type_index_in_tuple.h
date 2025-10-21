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
 
#ifndef SHARE_STORAGE_MULTI_DATA_SOURCE_MAP_TYPE_INDEX_IN_TUPLE_H
#define SHARE_STORAGE_MULTI_DATA_SOURCE_MAP_TYPE_INDEX_IN_TUPLE_H

#define NEED_MDS_REGISTER_DEFINE
#include "mds_register.h"
#undef NEED_MDS_REGISTER_DEFINE
#include "lib/container/ob_tuple.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{

template <typename K, typename V, bool MULTI_VERSION>
struct TypeHelper {};

template <typename FIRST, typename ...Args>
struct DropFirstElemtTuple { typedef common::ObTuple<Args...> type; };

#define GENERATE_TEST_MDS_TABLE
#define GENERATE_NORMAL_MDS_TABLE
#define GENERATE_LS_INNER_MDS_TABLE
#define _GENERATE_MDS_UNIT_(KEY_TYPE, VALUE_TYPE, NEED_MULTI_VERSION) \
,TypeHelper<KEY_TYPE, VALUE_TYPE, NEED_MULTI_VERSION>, \
TypeHelper<KEY_TYPE, VALUE_TYPE, !NEED_MULTI_VERSION>
typedef DropFirstElemtTuple<char
#include "mds_register.h"
>::type MdsTableTypeHelper;
#undef _GENERATE_MDS_UNIT_
#undef GENERATE_LS_INNER_MDS_TABLE
#undef GENERATE_NORMAL_MDS_TABLE
#undef GENERATE_TEST_MDS_TABLE

#define NEED_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION
#define _GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION_(HELPER_CLASS, BUFFER_CTX_TYPE, ID, ENUM_NAME) \
,BUFFER_CTX_TYPE
typedef DropFirstElemtTuple<char
#include "mds_register.h"
>::type BufferCtxTupleHelper;
#undef _GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION_
#undef NEED_GENERATE_MDS_FRAME_CODE_FOR_TRANSACTION

template <typename K, typename V>
inline constexpr bool get_multi_version_flag() {
  return (MdsTableTypeHelper::get_element_index<TypeHelper<K, V, true>>() & 1) == 0;
}

}
}
}
#endif
