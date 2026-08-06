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

#ifndef OB_STORAGE_DDL_DIRECT_LOAD_TYPE_H
#define OB_STORAGE_DDL_DIRECT_LOAD_TYPE_H

#include "common/ob_version_def.h"

namespace oceanbase
{
namespace storage
{

enum ObDirectLoadType {
  DIRECT_LOAD_INVALID = 0,
  IDEM_DIRECT_LOAD_DDL = 4,
  DIRECT_LOAD_MAX = 5
};
static int64_t DDL_IDEM_DATA_FORMAT_VERSION = DATA_CURRENT_VERSION;
static int64_t DDL_SLICE_BUCKET_NUM = 1007;
static inline bool is_complete_logic(const ObDirectLoadType &type)
{
  return ObDirectLoadType::IDEM_DIRECT_LOAD_DDL == type;
}
static inline bool is_valid_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::IDEM_DIRECT_LOAD_DDL == type;
}

static inline bool is_ddl_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::IDEM_DIRECT_LOAD_DDL == type;
}

static inline bool is_full_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::IDEM_DIRECT_LOAD_DDL == type;
}

static inline bool is_idem_type(const ObDirectLoadType &type)
{
  return ObDirectLoadType::IDEM_DIRECT_LOAD_DDL == type;
}

}  // end namespace storage
}  // end namespace oceanbase
   //
#endif  // OB_STORAGE_DDL_DIRECT_LOAD_TYPE_H
