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
  DIRECT_LOAD_DDL = 1,
  DIRECT_LOAD_DDL_V2 = 4,
  SN_IDEM_DIRECT_LOAD_DDL = 6,
  SS_IDEM_DIRECT_LOAD_DDL = 8,
  DIRECT_LOAD_MAX = 9
};
/* TODO@zhuoran.zzr wait to set as newest master version*/
static int64_t DDL_IDEM_DATA_FORMAT_VERSION = DATA_VERSION_1_0_0_0;
static int64_t DDL_SLICE_BUCKET_NUM = 1007;
static inline bool is_complete_logic(const ObDirectLoadType &type)
{
  return ObDirectLoadType::SN_IDEM_DIRECT_LOAD_DDL == type;
}
static inline bool is_valid_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_DDL == type ||
         ObDirectLoadType::DIRECT_LOAD_DDL_V2 == type ||
         ObDirectLoadType::SN_IDEM_DIRECT_LOAD_DDL == type ||
         ObDirectLoadType::SS_IDEM_DIRECT_LOAD_DDL == type;
}

static inline bool is_ddl_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_DDL == type ||
         ObDirectLoadType::DIRECT_LOAD_DDL_V2 == type ||
         SN_IDEM_DIRECT_LOAD_DDL == type ||
         SS_IDEM_DIRECT_LOAD_DDL == type;
}

static inline bool is_full_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_DDL == type
      || ObDirectLoadType::DIRECT_LOAD_DDL_V2 == type 
      || ObDirectLoadType::SN_IDEM_DIRECT_LOAD_DDL == type
      || ObDirectLoadType::SS_IDEM_DIRECT_LOAD_DDL == type;
}

static inline bool is_shared_storage_dempotent_mode(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_DDL_V2 == type;
}
static inline bool is_idem_type(const ObDirectLoadType &type)
{
  return SN_IDEM_DIRECT_LOAD_DDL == type ||
         SS_IDEM_DIRECT_LOAD_DDL == type ||
         DIRECT_LOAD_DDL_V2 == type;
}

}  // end namespace storage
}  // end namespace oceanbase
   //
#endif  // OB_STORAGE_DDL_DIRECT_LOAD_TYPE_H
