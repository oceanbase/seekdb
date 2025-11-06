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

#ifndef OCEANBASE_SHARE_OB_LOG_RESTORE_SOURCE_H_
#define OCEANBASE_SHARE_OB_LOG_RESTORE_SOURCE_H_

#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/worker.h"
#include "lib/allocator/page_arena.h"
#include "share/scn.h"
#include <cstdint>

namespace oceanbase
{
namespace share
{
enum class ObLogRestoreSourceType
{
  INVALID = 0,
  SERVICE = 1,
  LOCATION = 2,
  RAWPATH = 3,
  MAX = 4,
};

OB_INLINE bool is_valid_log_source_type(const ObLogRestoreSourceType &type)
{
  return type > ObLogRestoreSourceType::INVALID
    && type < ObLogRestoreSourceType::MAX;
}

OB_INLINE bool is_service_log_source_type(const ObLogRestoreSourceType &type)
{
  return type == ObLogRestoreSourceType::SERVICE;
}

OB_INLINE bool is_location_log_source_type(const ObLogRestoreSourceType &type)
{
  return type == ObLogRestoreSourceType::LOCATION;
}

OB_INLINE bool is_raw_path_log_source_type(const ObLogRestoreSourceType &type)
{
  return type == ObLogRestoreSourceType::RAWPATH;
}

struct ObLogRestoreSourceItem
{
  uint64_t tenant_id_;
  int64_t id_;
  ObLogRestoreSourceType type_;
  common::ObString value_;
  SCN until_scn_;
  common::ObArenaAllocator allocator_;
  ObLogRestoreSourceItem() :
    tenant_id_(),
    id_(),
    type_(ObLogRestoreSourceType::INVALID),
    until_scn_(),
    allocator_() {}
  ObLogRestoreSourceItem(const uint64_t tenant_id,
      const int64_t id,
      const SCN &until_scn) :
    tenant_id_(tenant_id),
    id_(id),
    type_(ObLogRestoreSourceType::INVALID),
    until_scn_(until_scn),
    allocator_() {}
  ObLogRestoreSourceItem(const uint64_t tenant_id,
      const int64_t id,
      const ObLogRestoreSourceType &type,
      const ObString &value,
      const SCN &until_scn) :
    tenant_id_(tenant_id),
    id_(id),
    type_(type),
    value_(value),
    until_scn_(until_scn),
    allocator_() {}
    ~ObLogRestoreSourceItem() {}
  bool is_valid() const;
  int deep_copy(ObLogRestoreSourceItem &other);
  static ObLogRestoreSourceType get_source_type(const ObString &type);
  static const char *get_source_type_str(const ObLogRestoreSourceType &type);
  TO_STRING_KV(K_(tenant_id), K_(id), K_(until_scn), K_(type), K_(value));
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogRestoreSourceItem);
};

} // namespace share
} // namespace oceanbase
#endif /* OCEANBASE_SHARE_OB_LOG_RESTORE_SOURCE_H_ */
