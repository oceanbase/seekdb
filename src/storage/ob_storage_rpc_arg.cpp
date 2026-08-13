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
#define USING_LOG_PREFIX RPC
#include "storage/ob_storage_rpc_arg.h"
namespace oceanbase
{
namespace obcall
{
using namespace oceanbase::storage;
OB_SERIALIZE_MEMBER(ObBatchGetTabletBindingRes, binding_datas_);

OB_DEF_SERIALIZE(ObDDLLocalBuildArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, source_tablet_id_, dest_tablet_id_,
    source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
    parallelism_, tablet_task_id_, data_format_version_,
    dest_schema_version_, lob_col_idxs_);
  return ret;
}
OB_DEF_DESERIALIZE(ObDDLLocalBuildArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, source_tablet_id_, dest_tablet_id_,
      source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
      parallelism_, tablet_task_id_, data_format_version_,
      dest_schema_version_, lob_col_idxs_);
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObDDLLocalBuildArg)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, source_tablet_id_, dest_tablet_id_,
    source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
    parallelism_, tablet_task_id_, data_format_version_,
    dest_schema_version_, lob_col_idxs_);
  return len;
}
bool ObDDLLocalBuildArg::is_valid() const
{
  bool is_valid = source_tablet_id_.is_valid() && dest_tablet_id_.is_valid()
               && OB_INVALID_ID != source_table_id_ && OB_INVALID_ID != dest_schema_id_
               && schema_version_ > 0 && snapshot_version_ > 0 && task_id_ > 0 && parallelism_ > 0
               && execution_id_ >= 0
               && tablet_task_id_ > 0 && data_format_version_ > 0
               && dest_schema_version_ > 0;
  return is_valid;
}
int ObDDLLocalBuildArg::assign(const ObDDLLocalBuildArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(other));
  } else if (OB_FAIL(lob_col_idxs_.assign(other.lob_col_idxs_))) {
  } else {
    source_tablet_id_ = other.source_tablet_id_;
    dest_tablet_id_ = other.dest_tablet_id_;
    source_table_id_ = other.source_table_id_;
    dest_schema_id_ = other.dest_schema_id_;
    schema_version_ = other.schema_version_;
    dest_schema_version_ = other.dest_schema_version_;
    snapshot_version_ = other.snapshot_version_;
    ddl_type_ = other.ddl_type_;
    task_id_ = other.task_id_;
    parallelism_ = other.parallelism_;
    execution_id_ = other.execution_id_;
    tablet_task_id_ = other.tablet_task_id_;
    data_format_version_ = other.data_format_version_;
  }
  return ret;
}
OB_SERIALIZE_MEMBER(ObDDLLocalBuildResult, ret_code_, row_inserted_, row_scanned_, physical_row_count_);
}  // namespace obcall
}  // namespace oceanbase
