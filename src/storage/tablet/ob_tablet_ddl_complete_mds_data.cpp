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
#include "storage/tablet/ob_tablet_ddl_complete_mds_data.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/ob_errno.h"
#include "storage/tx/ob_trans_define.h"
#include "storage/tx_storage/ob_ls_handle.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ddl/ob_direct_load_struct.h"

#define USING_LOG_PREFIX MDS

using namespace oceanbase::common;
using namespace oceanbase::transaction;

namespace oceanbase
{
namespace storage
{
ObTabletDDLCompleteMdsUserData::ObTabletDDLCompleteMdsUserData():
    has_complete_(false), direct_load_type_(ObDirectLoadType::DIRECT_LOAD_INVALID), data_format_version_(0),
    snapshot_version_(0), table_key_(), storage_schema_(), write_stat_(),
    allocator_("MdsDdlCom")
{}

ObTabletDDLCompleteMdsUserData::~ObTabletDDLCompleteMdsUserData()
{
  write_stat_.reset();
  storage_schema_.reset();
  allocator_.reset();
}

bool ObTabletDDLCompleteMdsUserData::is_valid() const
{
  return (!has_complete_) ||
         (has_complete_  && table_key_.is_valid()
                         && (direct_load_type_ > ObDirectLoadType::DIRECT_LOAD_INVALID &&
                             direct_load_type_ < ObDirectLoadType::DIRECT_LOAD_MAX)
                         && storage_schema_.is_valid() && write_stat_.is_valid())
          || (is_incremental_major_direct_load(direct_load_type_)
              && storage_schema_.is_valid());
}

int ObTabletDDLCompleteMdsUserData::set_storage_schema(const ObStorageSchema &other)
{
  int ret = OB_SUCCESS;
  if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(other));
  } else if (OB_FAIL(storage_schema_.assign(allocator_, other))) {
    LOG_WARN("failed to assign storage schema", K(ret));
  } else{
    for (int64_t i = 0; OB_SUCC(ret) && i < storage_schema_.column_array_.count(); ++i) {
      ObStorageColumnSchema &cs = storage_schema_.column_array_.at(i);
      cs.orig_default_value_.reset();
    }
  }
  return ret;
}

int ObTabletDDLCompleteMdsUserData::assign(const ObTabletDDLCompleteMdsUserData &other)
{
  int ret = OB_SUCCESS;
  if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(other));
  } else if (other.has_complete_ && OB_FAIL(set_storage_schema(other.storage_schema_))) {
    LOG_WARN("failed to set storage schema", K(ret));
  } else if (other.has_complete_ && OB_FAIL(write_stat_.assign(other.write_stat_))) {
    LOG_WARN("failed to set storage schema", K(ret));
  } else {
    has_complete_         = other.has_complete_;
    direct_load_type_     = other.direct_load_type_;
    data_format_version_  = other.data_format_version_;
    snapshot_version_     = other.snapshot_version_;
    table_key_            = other.table_key_;
  }
  return ret;
}

void ObTabletDDLCompleteMdsUserData::reset()
{
  has_complete_ = false;
  direct_load_type_ = ObDirectLoadType::DIRECT_LOAD_INVALID;
  data_format_version_ = 0;
  snapshot_version_ = 0;
  table_key_.reset();
  storage_schema_.reset();
  write_stat_.reset();
  allocator_.reset();
}

int ObTabletDDLCompleteMdsUserData::set_with_merge_arg(const ObTabletDDLCompleteArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else if (arg.has_complete_ && nullptr == arg.get_storage_schema()) {
    ret= OB_ERR_UNEXPECTED;
    LOG_WARN("schema should not be null", K(ret));
  } else if (arg.has_complete_ && OB_FAIL(set_storage_schema(*arg.get_storage_schema()))) {
    LOG_WARN("failed to set storage schema", K(ret), K(arg.get_storage_schema()));
  } else if (arg.has_complete_ && OB_FAIL(write_stat_.assign(arg.write_stat_))) {
    LOG_WARN("failed to set write stat", K(ret));
  } else {
    has_complete_ = arg.has_complete_;
    direct_load_type_ = arg.direct_load_type_;
    data_format_version_ = arg.data_format_version_;
    snapshot_version_ = arg.snapshot_version_;
    table_key_ = arg.table_key_;
  }
  return ret;
}

int64_t ObTabletDDLCompleteMdsUserData::get_serialize_size() const
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              has_complete_, direct_load_type_,
              data_format_version_, snapshot_version_,
              table_key_, write_stat_);
  if (has_complete_) {
    len += storage_schema_.get_serialize_size();
  }
  return len;
}

int ObTabletDDLCompleteMdsUserData::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, has_complete_, direct_load_type_,
              data_format_version_, snapshot_version_, table_key_,
              write_stat_);
  if (OB_FAIL(ret)) {
  } else if (has_complete_ && OB_FAIL(storage_schema_.serialize(buf, buf_len, pos))) {
    LOG_WARN("failed to serialize storage_schema", K(ret));
  }
  return ret;
}

int ObTabletDDLCompleteMdsUserData::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, has_complete_, direct_load_type_,
              data_format_version_, snapshot_version_, table_key_,
              write_stat_);
  if (OB_FAIL(ret)) {
  } else if (has_complete_ && OB_FAIL(storage_schema_.deserialize(allocator_, buf, data_len, pos))) {
    LOG_WARN("failed to deserialize stroage_schema", K(ret), KPC(this));
  }
  return ret;
}
} // namespace storage
} // namespace oceanbase
