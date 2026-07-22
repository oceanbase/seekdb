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
#include "storage/tablet/ob_batch_create_tablet_arg.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace storage;
namespace obcall
{
bool ObBatchCreateTabletArg::is_inited() const
{
  return major_frozen_scn_.is_valid();
}

bool ObBatchCreateTabletArg::is_valid() const
{
  bool valid = true;
  if (is_inited() && tablets_.count() > 0 && (create_tablet_schemas_.count() > 0 || table_schemas_.count() > 0)) {
    for (int64_t i = 0; valid && i < tablets_.count(); ++i) {
      const ObCreateTabletInfo &info = tablets_[i];
      const common::ObSArray<int64_t> &table_schema_index = info.table_schema_index_;
      if (!info.is_valid()) {
        valid = false;
      }

      for (int64_t j = 0; valid && j < table_schema_index.count(); ++j) {
        const int64_t index = table_schema_index[j];
        if (index < 0 || (index >= create_tablet_schemas_.count() && index >= table_schemas_.count())) {
          valid = false;
        }
      }
    }
  } else {
    valid = false;
  }
  return valid;
}

void ObBatchCreateTabletArg::reset()
{
  major_frozen_scn_.reset();
  tablets_.reset();
  table_schemas_.reset();
  need_check_tablet_cnt_ = false;
  for (int64_t i = 0; i < create_tablet_schemas_.count(); ++i) {
    create_tablet_schemas_[i]->~ObCreateTabletSchema();
  }
  create_tablet_schemas_.reset();
  allocator_.reset();
  tablet_extra_infos_.reset();
  clog_checkpoint_scn_.reset();
  mds_checkpoint_scn_.reset();
  create_type_ = ObTabletMdsUserDataType::CREATE_TABLET;
}

int ObBatchCreateTabletArg::assign(const ObBatchCreateTabletArg &arg)
{
  int ret = OB_SUCCESS;
  const common::ObSArray<storage::ObCreateTabletSchema*> &create_tablet_schemas = arg.create_tablet_schemas_;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invalid", KR(ret), K(arg));
  } else if (OB_FAIL(tablets_.assign(arg.tablets_))) {
    LOG_WARN("failed to assign tablets", KR(ret), K(arg));
  } else if (OB_FAIL(table_schemas_.assign(arg.table_schemas_))) {
    LOG_WARN("failed to assign table schema", KR(ret), K(arg));
  } else if (OB_FAIL(tablet_extra_infos_.assign(arg.tablet_extra_infos_))) {
    LOG_WARN("failed to assign tablet extra infos", K(ret), K(arg));
  } else if (OB_FAIL(create_tablet_schemas_.reserve(create_tablet_schemas.count()))) {
    STORAGE_LOG(WARN, "Fail to reserve schema array", K(ret), K(create_tablet_schemas.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < create_tablet_schemas.count(); ++i) {
      if (OB_ISNULL(create_tablet_schemas[i])) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", KR(ret), K(i), KPC(this));
      } else {
        ObCreateTabletSchema *create_tablet_schema = NULL;
        void *create_tablet_schema_ptr = allocator_.alloc(sizeof(ObCreateTabletSchema));
        if (OB_ISNULL(create_tablet_schema_ptr)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate storage schema", KR(ret));
        } else if (FALSE_IT(create_tablet_schema = new (create_tablet_schema_ptr)ObCreateTabletSchema())) {
        } else if (OB_FAIL(create_tablet_schema->init(allocator_, *create_tablet_schemas[i]))) {
          create_tablet_schema->~ObCreateTabletSchema();
          STORAGE_LOG(WARN,"Fail to init create_tablet_schema", K(ret));
        } else if (OB_FAIL(create_tablet_schemas_.push_back(create_tablet_schema))) {
          create_tablet_schema->~ObCreateTabletSchema();
          STORAGE_LOG(WARN, "Fail to add schema", K(ret));
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    reset();
  } else {
    major_frozen_scn_ = arg.major_frozen_scn_;
    need_check_tablet_cnt_ = arg.need_check_tablet_cnt_;
    clog_checkpoint_scn_ = arg.clog_checkpoint_scn_;
    mds_checkpoint_scn_ = arg.mds_checkpoint_scn_;
    create_type_ = arg.create_type_;
  }
  return ret;
}

bool ObBatchCreateTabletArg::set_binding_info_outside_create() const
{
  int bool_ret = false;
  uint64_t min_data_version = UINT64_MAX;
  for (int64_t i = 0; i < tablet_extra_infos_.count(); i++) {
    min_data_version = std::min(min_data_version, tablet_extra_infos_.at(i).data_format_version_);
  }
  if (UINT64_MAX != min_data_version) {
    bool_ret = true;
  }
  return bool_ret;
}


int ObBatchCreateTabletArg::init_create_tablet(
  const SCN &major_frozen_scn,
  const bool need_check_tablet_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!major_frozen_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(major_frozen_scn));
  } else {
    /*
      To fix issue 2025022400107312907
      major in new tablet should be larger than last freeze info
      to disable checksum validation between global index with truncate info and truncated tablet in data table
    */
    if (major_frozen_scn.get_val_for_tx() > 1) {
      major_frozen_scn_ = SCN::scn_inc(major_frozen_scn);
    } else {
      major_frozen_scn_ = major_frozen_scn;
    }
    need_check_tablet_cnt_ = need_check_tablet_cnt;
  }
  return ret;
}

int64_t ObBatchCreateTabletArg::get_tablet_count() const
{
  int64_t cnt = 0;
  for (int64_t i = 0; i < tablets_.count(); ++i) {
    const ObCreateTabletInfo &info = tablets_[i];
    cnt += info.get_tablet_count();
  }
  return cnt;
}

int ObBatchCreateTabletArg::serialize_for_create_tablet_schemas(char *buf,
    const int64_t data_len,
    int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(serialization::encode_vi64(buf, data_len, pos, create_tablet_schemas_.count()))) {
    STORAGE_LOG(WARN, "failed to encode schema count", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < create_tablet_schemas_.count(); ++i) {
    if (OB_ISNULL(create_tablet_schemas_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", KR(ret), K(i), KPC(this));
    } else if (OB_FAIL(create_tablet_schemas_.at(i)->serialize(buf, data_len, pos))) {
      STORAGE_LOG(WARN, "failed to serialize schema", K(ret));
    }
  }
  return ret;
}

int64_t ObBatchCreateTabletArg::get_serialize_size_for_create_tablet_schemas() const
{
  int ret = OB_SUCCESS;
  int64_t len = 0;
  len += serialization::encoded_length_vi64(create_tablet_schemas_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < create_tablet_schemas_.count(); ++i) {
    if (OB_ISNULL(create_tablet_schemas_.at(i))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("create_tablet_schema is NULL", KR(ret), K(i), KPC(this));
    } else {
      len += create_tablet_schemas_.at(i)->get_serialize_size();
    }
  }
  return len;
}

int ObBatchCreateTabletArg::deserialize_create_tablet_schemas(const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0) || OB_UNLIKELY(pos > data_len)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(buf), K(data_len), K(pos), K(ret));
  } else if (pos == data_len) {
    //do nothing
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
    STORAGE_LOG(WARN, "failed to decode schema count", K(ret));
  } else if (count < 0) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "count invalid", KR(ret), K(buf), K(data_len), K(pos), K(count));
  } else if (count == 0) {
    STORAGE_LOG(INFO, "upgrade, count is 0", KR(ret), K(buf), K(data_len), K(pos), K(count));
  } else if (OB_FAIL(create_tablet_schemas_.reserve(count))) {
    STORAGE_LOG(WARN, "failed to reserve schema array", K(ret), K(count), K(buf), K(data_len), K(pos));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      ObCreateTabletSchema *create_tablet_schema = NULL;
      void *create_tablet_schema_ptr = allocator_.alloc(sizeof(ObCreateTabletSchema));
      if (OB_ISNULL(create_tablet_schema_ptr)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate storage schema", KR(ret));
      } else if (FALSE_IT(create_tablet_schema = new (create_tablet_schema_ptr)ObCreateTabletSchema())) {
      } else if (OB_FAIL(create_tablet_schema->deserialize(allocator_, buf, data_len, pos))) {
        create_tablet_schema->~ObCreateTabletSchema();
        STORAGE_LOG(WARN,"failed to deserialize schema", K(ret), K(i), K(count), K(buf), K(data_len), K(pos));
      } else if (OB_FAIL(create_tablet_schemas_.push_back(create_tablet_schema))) {
        create_tablet_schema->~ObCreateTabletSchema();
        STORAGE_LOG(WARN, "failed to add schema", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
      reset();
    }
  }
  return ret;
}

DEF_TO_STRING(ObBatchCreateTabletArg)
{
  int64_t pos = 0;
  J_KV(K_(major_frozen_scn), K_(need_check_tablet_cnt), K_(tablets), K_(tablet_extra_infos), K_(clog_checkpoint_scn), K_(create_type), K_(mds_checkpoint_scn));
  return pos;
}

OB_DEF_SERIALIZE(ObBatchCreateTabletArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, major_frozen_scn_, tablets_, table_schemas_, need_check_tablet_cnt_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(serialize_for_create_tablet_schemas(buf, buf_len, pos))) {
    LOG_WARN("failed to serialize_for_create_tablet_schemas", KR(ret), KPC(this));
  } else {
    OB_UNIS_ENCODE_ARRAY(tablet_extra_infos_, tablet_extra_infos_.count());
  }
  LST_DO_CODE(OB_UNIS_ENCODE, clog_checkpoint_scn_, create_type_, mds_checkpoint_scn_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObBatchCreateTabletArg)
{
  int len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, major_frozen_scn_, tablets_, table_schemas_, need_check_tablet_cnt_);
  len += get_serialize_size_for_create_tablet_schemas();
  OB_UNIS_ADD_LEN_ARRAY(tablet_extra_infos_, tablet_extra_infos_.count());
  LST_DO_CODE(OB_UNIS_ADD_LEN, clog_checkpoint_scn_, create_type_, mds_checkpoint_scn_);
  return len;
}

OB_DEF_DESERIALIZE(ObBatchCreateTabletArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, major_frozen_scn_, tablets_, table_schemas_, need_check_tablet_cnt_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(deserialize_create_tablet_schemas(buf, data_len, pos))) {
    LOG_WARN("failed to deserialize_for_create_tablet_schemas", KR(ret));
  } else {
    int64_t tablet_extra_infos_count = 0;
    OB_UNIS_DECODE(tablet_extra_infos_count);
    if (tablet_extra_infos_count <= 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("tablet extra infos are required", K(ret), K(tablet_extra_infos_count));
    } else if (OB_FAIL(tablet_extra_infos_.prepare_allocate(tablet_extra_infos_count))) {
      LOG_WARN("prepare allocate failed", K(ret), K(tablet_extra_infos_count));
    } else {
      OB_UNIS_DECODE_ARRAY(tablet_extra_infos_, tablet_extra_infos_count);
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE, clog_checkpoint_scn_, create_type_, mds_checkpoint_scn_);
  return ret;
}

}  // namespace obcall
}  // namespace oceanbase
