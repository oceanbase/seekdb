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

<<<<<<<< HEAD:src/share/ob_module_data_arg.cpp
#define USING_LOG_PREFIX SHARE

#include "share/ob_module_data_arg.h"

namespace oceanbase
{
namespace table
{

bool ObModuleDataArg::is_valid() const
{
  return op_ > ObModuleDataArg::INVALID_OP
      && op_ < ObModuleDataArg::MAX_OP
      && target_tenant_id_ != OB_INVALID_TENANT_ID
      && module_ > ObModuleDataArg::INVALID_MOD
      && module_ < ObModuleDataArg::MAX_MOD;
}

}  // namespace table
}  // namespace oceanbase
========
// Definition site for vector-decode locator group B (see ob_vector_decode_util.cpp).
#define OB_VEC_INST_B
#include "ob_vector_decode_util.h"
namespace oceanbase
{
namespace blocksstable
{

}
};
>>>>>>>> origin/master:src/storage/blocksstable/encoding/ob_vector_decode_util_b.cpp
