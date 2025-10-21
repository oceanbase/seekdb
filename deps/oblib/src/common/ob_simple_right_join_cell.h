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

#ifndef OB_SIMPLE_RIGHT_JOIN_CELL_H_
#define OB_SIMPLE_RIGHT_JOIN_CELL_H_

#include "lib/container/ob_vector.h"
#include "common/object/ob_object.h"
#include "common/rowkey/ob_rowkey.h"

namespace oceanbase
{
namespace common
{
struct ObSimpleRightJoinCell
{
  uint64_t table_id;
  ObRowkey rowkey;
};
template <>
struct ob_vector_traits<ObSimpleRightJoinCell>
{
  typedef ObSimpleRightJoinCell *pointee_type;
  typedef ObSimpleRightJoinCell value_type;
  typedef const ObSimpleRightJoinCell const_value_type;
  typedef value_type *iterator;
  typedef const value_type *const_iterator;
  typedef int32_t difference_type;
};
}
}

#endif //OB_SIMPLE_RIGHT_JOIN_CELL_H_

