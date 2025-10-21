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

#define USING_LOG_PREFIX SHARE
#include "ob_log_restore_source.h"
#include "share/ob_define.h"

using namespace oceanbase::share;
static const char* LogRestoreSourceTypeArray[static_cast<int64_t>(ObLogRestoreSourceType::MAX)] = {
  "INVALID",
  "SERVICE",
  "LOCATION",
  "RAWPATH",
};

bool ObLogRestoreSourceItem::is_valid() const
{
  return id_ > 0 && is_valid_log_source_type(type_) && !value_.empty() && until_scn_.is_valid();
}

ObLogRestoreSourceType ObLogRestoreSourceItem::get_source_type(const ObString &type_str)
{
  ObLogRestoreSourceType type = ObLogRestoreSourceType::INVALID;
  for (int64_t i = 0; i < static_cast<int64_t>(ObLogRestoreSourceType::MAX); i++) {
    if (OB_NOT_NULL(LogRestoreSourceTypeArray[i])
        && 0 == type_str.case_compare(LogRestoreSourceTypeArray[i])) {
      type = static_cast<ObLogRestoreSourceType>(i);
      break;
    }
  }
  return type;
}

const char *ObLogRestoreSourceItem::get_source_type_str(const ObLogRestoreSourceType &type)
{
  const char* str = NULL;
  if (type >= ObLogRestoreSourceType::INVALID && type < ObLogRestoreSourceType::MAX) {
    str = LogRestoreSourceTypeArray[static_cast<int64_t>(type)];
  }
  return str;
}

int ObLogRestoreSourceItem::deep_copy(ObLogRestoreSourceItem &other)
{
  int ret = OB_SUCCESS;
  tenant_id_ = other.tenant_id_;
  id_ = other.id_;
  type_ = other.type_;
  until_scn_ = other.until_scn_;
  OZ (ob_write_string(allocator_, other.value_, value_));
  return ret;
}
