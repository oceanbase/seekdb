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

#ifndef OCEANBASE_LIB_OB_SPARSE_VECTOR_IP_DISTANCE_H_
#define OCEANBASE_LIB_OB_SPARSE_VECTOR_IP_DISTANCE_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log.h"
#include "lib/ob_define.h"
#include "common/object/ob_obj_compare.h"
#include "ob_vector_op_common.h"
#include "lib/udt/ob_map_type.h"

namespace oceanbase
{
namespace common
{
struct ObSparseVectorIpDistance
{
  static int spiv_ip_distance_func(const ObMapType *a, const ObMapType *b, double &distance);
  // OB_INLINE static int spiv_ip_distance_normal(const ObMapType *a, const ObMapType *b, double &distance);
};


}  // namespace common
}  // namespace oceanbase
#endif

