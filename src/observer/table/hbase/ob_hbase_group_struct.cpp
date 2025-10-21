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

#define USING_LOG_PREFIX SERVER

#include "ob_hbase_group_struct.h"

using namespace oceanbase::table;
using namespace oceanbase::common;

uint64_t ObHbaseGroupKey::hash() const
{
  uint64_t hash_val = 0;
  uint64_t seed = 0;
  hash_val = murmurhash(&ls_id_, sizeof(ls_id_), seed);
  hash_val = murmurhash(&table_id_, sizeof(table_id_), hash_val);
  hash_val = murmurhash(&schema_version_, sizeof(schema_version_), hash_val);
  hash_val = murmurhash(&op_type_, sizeof(op_type_), hash_val);
  return hash_val;
}

int ObHbaseGroupKey::deep_copy(common::ObIAllocator &allocator, const ObITableGroupKey &other)
{
  int ret = OB_SUCCESS;
  const ObHbaseGroupKey &other_key = static_cast<const ObHbaseGroupKey &>(other);
  ls_id_ = other_key.ls_id_;
  table_id_ = other_key.table_id_;
  schema_version_ = other_key.schema_version_;
  op_type_ = other_key.op_type_;
  return ret;
}

bool ObHbaseGroupKey::is_equal(const ObITableGroupKey &other) const
{
  const ObHbaseGroupKey &other_key = static_cast<const ObHbaseGroupKey &>(other);
  return type_ == other.type_
    && ls_id_ == static_cast<const ObHbaseGroupKey &>(other).ls_id_
    && table_id_ == static_cast<const ObHbaseGroupKey &>(other).table_id_
    && schema_version_ == static_cast<const ObHbaseGroupKey &>(other).schema_version_
    && op_type_ == static_cast<const ObHbaseGroupKey &>(other).op_type_;
}


