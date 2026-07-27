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

#include <cstdint>

namespace oceanbase
{
namespace storage
{

enum ObDirectLoadType {
  DIRECT_LOAD_INVALID = 0,
  DIRECT_LOAD_DDL = 1,
  DIRECT_LOAD_LOAD_DATA = 2,
  DIRECT_LOAD_INCREMENTAL = 3,
  SN_IDEM_DIRECT_LOAD_DDL = 4,
  SN_IDEM_DIRECT_LOAD_DATA = 5,
  DIRECT_LOAD_INCREMENTAL_MAJOR = 6,
  DIRECT_LOAD_MAX
};
static int64_t DDL_TABLET_BUCKET_NUM = 1007;
static int64_t DDL_SLICE_BUCKET_NUM = 1007;
static inline bool is_complete_logic(const ObDirectLoadType &type)
{
  return ObDirectLoadType::SN_IDEM_DIRECT_LOAD_DDL == type ||
         ObDirectLoadType::SN_IDEM_DIRECT_LOAD_DATA == type;
}
static inline bool is_valid_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_INVALID < type &&
         ObDirectLoadType::DIRECT_LOAD_MAX > type;
}

static inline bool is_ddl_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_DDL == type ||
         SN_IDEM_DIRECT_LOAD_DDL == type;
}

static inline bool is_full_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_DDL == type
      || ObDirectLoadType::DIRECT_LOAD_LOAD_DATA == type
      || ObDirectLoadType::SN_IDEM_DIRECT_LOAD_DDL == type
      || ObDirectLoadType::SN_IDEM_DIRECT_LOAD_DATA == type;
}

static inline bool is_data_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_LOAD_DATA == type
      || ObDirectLoadType::DIRECT_LOAD_INCREMENTAL == type
      || ObDirectLoadType::SN_IDEM_DIRECT_LOAD_DATA == type
      || ObDirectLoadType::DIRECT_LOAD_INCREMENTAL_MAJOR == type;
}

static inline bool is_incremental_minor_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_INCREMENTAL == type;
}

static inline bool is_incremental_major_direct_load(const ObDirectLoadType &type)
{
  return ObDirectLoadType::DIRECT_LOAD_INCREMENTAL_MAJOR == type;
}

static inline bool is_incremental_direct_load(const ObDirectLoadType &type)
{
  return is_incremental_minor_direct_load(type);
}

static inline bool is_idem_type(const ObDirectLoadType &type)
{
  return SN_IDEM_DIRECT_LOAD_DDL == type || SN_IDEM_DIRECT_LOAD_DATA == type;
}

}  // end namespace storage
}  // end namespace oceanbase
   //
#endif  // OB_STORAGE_DDL_DIRECT_LOAD_TYPE_H
