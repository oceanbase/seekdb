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

#define USING_LOG_PREFIX STORAGE

#include "storage/ddl/ob_table_fork_info.h"
#include "storage/ddl/ob_tablet_fork_task.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace storage
{

ObTableForkInfo::ObTableForkInfo()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    table_id_(OB_INVALID_ID),
    schema_version_(0), task_id_(0),
    source_tablet_ids_(), dest_tablet_ids_(),
    fork_snapshot_version_(0),
    compat_mode_(lib::Worker::CompatMode::INVALID),
    data_format_version_(0), consumer_group_id_(0)
{
}

ObTableForkInfo::ObTableForkInfo(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const uint64_t table_id,
    const int64_t schema_version,
    const int64_t task_id,
    const int64_t fork_snapshot_version,
    const lib::Worker::CompatMode compat_mode,
    const int64_t data_format_version,
    const int64_t consumer_group_id,
    const common::ObIArray<common::ObTabletID> &source_tablet_ids,
    const common::ObIArray<common::ObTabletID> &dest_tablet_ids)
  : tenant_id_(tenant_id),
    ls_id_(ls_id),
    table_id_(table_id),
    schema_version_(schema_version),
    task_id_(task_id),
    source_tablet_ids_(),
    dest_tablet_ids_(),
    fork_snapshot_version_(fork_snapshot_version),
    compat_mode_(compat_mode),
    data_format_version_(data_format_version),
    consumer_group_id_(consumer_group_id)
{
  (void)source_tablet_ids_.assign(source_tablet_ids);
  (void)dest_tablet_ids_.assign(dest_tablet_ids);
}

int ObTableForkInfo::assign(const ObTableForkInfo &info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(info));
  } else if (OB_FAIL(source_tablet_ids_.assign(info.source_tablet_ids_))) {
    LOG_WARN("failed to assign source tablet ids", K(ret));
  } else if (OB_FAIL(dest_tablet_ids_.assign(info.dest_tablet_ids_))) {
    LOG_WARN("failed to assign dest tablet ids", K(ret));
  } else {
    tenant_id_ = info.tenant_id_;
    ls_id_ = info.ls_id_;
    table_id_ = info.table_id_;
    schema_version_ = info.schema_version_;
    task_id_ = info.task_id_;
    fork_snapshot_version_ = info.fork_snapshot_version_;
    compat_mode_ = info.compat_mode_;
    data_format_version_ = info.data_format_version_;
    consumer_group_id_ = info.consumer_group_id_;
  }
  return ret;
}

bool ObTableForkInfo::is_valid() const
{
  return OB_INVALID_ID != tenant_id_
      && ls_id_.is_valid()
      && OB_INVALID_ID != table_id_
      && schema_version_ > 0
      && task_id_ > 0
      && source_tablet_ids_.count() > 0
      && dest_tablet_ids_.count() > 0
      && source_tablet_ids_.count() == dest_tablet_ids_.count()
      && fork_snapshot_version_ > 0
      && compat_mode_ != lib::Worker::CompatMode::INVALID
      && data_format_version_ > 0
      && consumer_group_id_ >= 0;
}

int ObTableForkInfo::generate_fork_params(common::ObIArray<ObTabletForkParam> &params) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fork info", K(ret), K(*this));
  } else if (OB_UNLIKELY(source_tablet_ids_.count() != dest_tablet_ids_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source and dest tablet ids count mismatch", K(ret), K_(source_tablet_ids), K_(dest_tablet_ids));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < source_tablet_ids_.count(); ++i) {
      ObTabletForkParam fork_param;
      fork_param.tenant_id_ = tenant_id_;
      fork_param.ls_id_ = ls_id_;
      fork_param.table_id_ = table_id_;
      fork_param.schema_version_ = schema_version_;
      fork_param.task_id_ = task_id_;
      fork_param.source_tablet_id_ = source_tablet_ids_.at(i);
      fork_param.dest_tablet_id_ = dest_tablet_ids_.at(i);
      fork_param.fork_snapshot_version_ = fork_snapshot_version_;
      fork_param.compat_mode_ = compat_mode_;
      fork_param.data_format_version_ = data_format_version_;
      fork_param.consumer_group_id_ = consumer_group_id_;
      fork_param.is_inited_ = true;
      if (OB_FAIL(params.push_back(fork_param))) {
        LOG_WARN("failed to push back fork param", K(ret));
      }
    }
  }
  return ret;
}

int ObTableForkInfo::get_tablet_fork_param(
    const common::ObTabletID &tablet_id,
    ObTabletForkParam &tablet_fork_param) const
{
  int ret = OB_SUCCESS;
  int64_t found_idx = -1;
  if (OB_UNLIKELY(!is_valid() || !tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(*this), K(tablet_id));
  } else if (OB_UNLIKELY(source_tablet_ids_.count() != dest_tablet_ids_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("source and dest tablet ids count mismatch", K(ret), K_(source_tablet_ids), K_(dest_tablet_ids));
  } else {
    for (int64_t i = 0; i < source_tablet_ids_.count(); ++i) {
      if (source_tablet_ids_.at(i) == tablet_id) {
        found_idx = i;
        break;
      }
    }
    if (found_idx < 0) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_INFO("tablet not in fork info", K(ret), K(tablet_id), K_(task_id), K_(table_id), K_(ls_id));
    } else {
      tablet_fork_param.reset();
      tablet_fork_param.tenant_id_ = tenant_id_;
      tablet_fork_param.ls_id_ = ls_id_;
      tablet_fork_param.table_id_ = table_id_;
      tablet_fork_param.schema_version_ = schema_version_;
      tablet_fork_param.task_id_ = task_id_;
      tablet_fork_param.source_tablet_id_ = source_tablet_ids_.at(found_idx);
      tablet_fork_param.dest_tablet_id_ = dest_tablet_ids_.at(found_idx);
      tablet_fork_param.fork_snapshot_version_ = fork_snapshot_version_;
      tablet_fork_param.compat_mode_ = compat_mode_;
      tablet_fork_param.data_format_version_ = data_format_version_;
      tablet_fork_param.consumer_group_id_ = consumer_group_id_;
      tablet_fork_param.is_inited_ = true;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObTableForkInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, tenant_id_, ls_id_, table_id_, schema_version_, task_id_,
              source_tablet_ids_, dest_tablet_ids_, fork_snapshot_version_,
              compat_mode_, data_format_version_, consumer_group_id_);
  return ret;
}

OB_DEF_DESERIALIZE(ObTableForkInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, tenant_id_, ls_id_, table_id_, schema_version_, task_id_,
              source_tablet_ids_, dest_tablet_ids_, fork_snapshot_version_,
              compat_mode_, data_format_version_, consumer_group_id_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObTableForkInfo)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, tenant_id_, ls_id_, table_id_, schema_version_, task_id_,
              source_tablet_ids_, dest_tablet_ids_, fork_snapshot_version_,
              compat_mode_, data_format_version_, consumer_group_id_);
  return len;
}

} // namespace storage
} // namespace oceanbase
