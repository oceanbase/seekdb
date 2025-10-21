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

#include "ob_replica_define.h"

namespace oceanbase
{
namespace common
{

OB_SERIALIZE_MEMBER(ObReplicaProperty, property_);

int ObReplicaProperty::set_memstore_percent(int64_t memstore_percent)
{
  int ret = OB_SUCCESS;

  if (memstore_percent >= 0 && memstore_percent <= 100) {
    memstore_percent_ = memstore_percent & 0x7f;
  } else {
    ret = OB_INVALID_ARGUMENT;
  }

  return ret;
}

} // common
} // oceanbase
