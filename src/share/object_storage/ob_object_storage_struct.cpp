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
#include "ob_object_storage_struct.h"
#include "rootserver/ob_root_service.h"
#ifdef OB_BUILD_SHARED_STORAGE
#include "storage/shared_storage/ob_ss_format_util.h"
#endif

using namespace oceanbase;
using namespace lib;
using namespace common;
using namespace share;

static const char *access_mode_strs[] = {"access_mode=access_by_id",
                                         "access_mode=access_by_ram_url"};


static const char *state_strs[] = {"ADDING", "ADDED", "DROPPING", "DROPPED", "CHANGING", "CHANGED"};

const char *ObZoneStorageState::get_str(const STATE &state)
{
  const char *str = nullptr;
  if (state < 0 || state >= MAX) {
    str = "UNKNOWN";
  } else {
    str = state_strs[state];
  }
  return str;
}

ObZoneStorageState::STATE ObZoneStorageState::get_state(const char *state_str)
{
  ObZoneStorageState::STATE state = ObZoneStorageState::MAX;
  const int64_t count = ARRAYSIZEOF(state_strs);
  STATIC_ASSERT(static_cast<int64_t>(ObZoneStorageState::MAX) == count, "state count mismatch");
  for (int64_t i = 0; i < count; ++i) {
    if (0 == strcmp(state_str, state_strs[i])) {
      state = static_cast<ObZoneStorageState::STATE>(i);
      break;
    }
  }
  return state;
}

static const char *type_strs[] = {"DATA", "LOG", "ALL"};

const char *ObStorageUsedType::get_str(const TYPE &type)
{
  const char *str = nullptr;
  if (type < 0 || type >= TYPE::USED_TYPE_MAX) {
    str = "UNKNOWN";
  } else {
    str = type_strs[type];
  }
  return str;
}

ObStorageUsedType::TYPE ObStorageUsedType::get_type(const char *type_str)
{
  ObStorageUsedType::TYPE type = ObStorageUsedType::TYPE::USED_TYPE_MAX;

  const int64_t count = ARRAYSIZEOF(type_strs);
  STATIC_ASSERT(static_cast<int64_t>(ObStorageUsedType::TYPE::USED_TYPE_MAX) == count,
                "type count mismatch");
  for (int64_t i = 0; i < count; ++i) {
    if (0 == STRCASECMP(type_str, type_strs[i])) {
      type = static_cast<ObStorageUsedType::TYPE>(i);
      break;
    }
  }
  return type;
}

static const char *scope_type_strs[] = {"ZONE", "REGION"};

OB_SERIALIZE_MEMBER(ObStorageDestAttr, path_, endpoint_, authorization_, extension_);
void ObStorageDestAttr::reset()
{
  path_[0] = '\0';
  endpoint_[0] = '\0';
  authorization_[0] = '\0';
  extension_[0] = '\0';
}

ObStorageDestAttr &ObStorageDestAttr::operator=(const ObStorageDestAttr &dest_attr)
{
  if (this != &dest_attr) {
    MEMCPY(path_, dest_attr.path_, sizeof(dest_attr.path_));
    MEMCPY(endpoint_, dest_attr.endpoint_, sizeof(dest_attr.endpoint_));
    MEMCPY(authorization_, dest_attr.authorization_, sizeof(dest_attr.authorization_));
    MEMCPY(extension_, dest_attr.extension_, sizeof(dest_attr.extension_));
  }
  return *this;
}

bool ObStorageDestAttr::is_valid() const
{
  return 0 != strlen(path_) &&
         ObString(path_).prefix_match(OB_FILE_PREFIX) ?
         (0 == strlen(endpoint_) && 0 == strlen(authorization_)) :
         (0 != strlen(endpoint_) && 0 != strlen(authorization_));
}

OB_SERIALIZE_MEMBER(ObZoneStorageTableInfo, dest_attr_, state_, used_for_, storage_id_, op_id_,
                    sub_op_id_, zone_, max_iops_, max_bandwidth_);
void ObZoneStorageTableInfo::reset()
{
  dest_attr_.reset();
  state_ = ObZoneStorageState::MAX;
  used_for_ = ObStorageUsedType::USED_TYPE_MAX;
  storage_id_ = OB_INVALID_ID;
  op_id_ = OB_INVALID_ID;
  sub_op_id_ = OB_INVALID_ID;
  zone_.reset();
  max_iops_ = OB_INVALID_MAX_IOPS;
  max_bandwidth_ = OB_INVALID_MAX_BANDWIDTH;
}

ObZoneStorageTableInfo &ObZoneStorageTableInfo::operator=(const ObZoneStorageTableInfo &table_info)
{
  if (this != &table_info) {
    dest_attr_ = table_info.dest_attr_;
    state_ = table_info.state_;
    used_for_ = table_info.used_for_;
    storage_id_ = table_info.storage_id_;
    op_id_ = table_info.op_id_;
    sub_op_id_ = table_info.sub_op_id_;
    zone_ = table_info.zone_;
    max_iops_ = table_info.max_iops_;
    max_bandwidth_ = table_info.max_bandwidth_;
  }
  return *this;
}

bool ObZoneStorageTableInfo::is_valid() const
{
  return dest_attr_.is_valid() && ObZoneStorageState::is_valid(state_) &&
         ObStorageUsedType::is_valid(used_for_) && !zone_.is_empty() &&
         storage_id_ != OB_INVALID_ID && op_id_ != OB_INVALID_ID && max_iops_ >= 0 && max_bandwidth_ >= 0;
}

ObZoneStorageOperationTableInfo::ObZoneStorageOperationTableInfo()
  : op_id_(UINT64_MAX), sub_op_id_(UINT64_MAX), op_type_(ObZoneStorageState::MAX)
{
}

bool ObDeviceConfig::is_valid() const
{
  bool bool_ret = true;
  if ((STRLEN(used_for_) <= 0) || (STRLEN(path_) <= 0)
      || (STRLEN(state_) <= 0) || (create_timestamp_ < 0)
      || (last_check_timestamp_ < 0)
      || (UINT64_MAX == op_id_)
      || (UINT64_MAX == sub_op_id_)
      || (UINT64_MAX == storage_id_)
      || (max_iops_ < 0)
      || (max_bandwidth_ < 0)) {
    bool_ret = false;
  } else {
    if (ObString(path_).prefix_match(OB_FILE_PREFIX)) {
      if ((STRLEN(endpoint_) > 0) || (STRLEN(access_info_) > 0)) {
        bool_ret = false;
      }
    } else {
      if ((STRLEN(endpoint_) <= 0) || (STRLEN(access_info_) <= 0)) {
        bool_ret = false;
      }
    }
  }
  return bool_ret;
}
