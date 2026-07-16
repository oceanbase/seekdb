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

#ifdef OB_BUILD_SHARED_STORAGE
OB_SERIALIZE_MEMBER(ObGetSSMacroBlockArg, tenant_id_, macro_id_, offset_, size_);
OB_DEF_SERIALIZE(ObGetSSMacroBlockResult)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              macro_buf_);
  return ret;
}

#endif
#ifdef OB_BUILD_SHARED_STORAGE
OB_DEF_DESERIALIZE(ObGetSSMacroBlockResult)
{
  int ret = OB_SUCCESS;
  ObString tmp_str;
  LST_DO_CODE(OB_UNIS_DECODE,
        tmp_str);
  if (OB_FAIL(ob_write_string(allocator_, tmp_str, macro_buf_))) {
    LOG_WARN("failed to copy string", KR(ret), K(tmp_str));
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObGetSSMacroBlockResult)
{
  int len = 0;
    LST_DO_CODE(OB_UNIS_ADD_LEN,
          macro_buf_);
  return len;
}


OB_SERIALIZE_MEMBER(ObGetSSPhyBlockInfoArg, tenant_id_, phy_block_idx_);
OB_SERIALIZE_MEMBER(ObGetSSPhyBlockInfoResult, ss_phy_block_info_, ret_);

OB_SERIALIZE_MEMBER(ObSSMicroMetaInfo, reuse_version_, data_dest_, access_time_, length_, is_in_l1_, is_in_ghost_,
    is_persisted_, is_reorganizing_, ref_cnt_, crc_, micro_key_);
OB_SERIALIZE_MEMBER(ObGetSSMicroBlockMetaArg, tenant_id_, micro_key_);
OB_SERIALIZE_MEMBER(ObGetSSMicroBlockMetaResult, micro_meta_info_, ret_);

OB_SERIALIZE_MEMBER(ObGetSSMacroBlockByURIArg, tenant_id_, uri_, offset_, size_);
OB_DEF_SERIALIZE(ObGetSSMacroBlockByURIResult)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              macro_buf_);
  return ret;
}

OB_DEF_DESERIALIZE(ObGetSSMacroBlockByURIResult)
{
  int ret = OB_SUCCESS;
  ObString tmp_str;
  LST_DO_CODE(OB_UNIS_DECODE,
        tmp_str);
  if (OB_FAIL(ob_write_string(allocator_, tmp_str, macro_buf_))) {
    LOG_WARN("failed to copy string", KR(ret), K(tmp_str));
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObGetSSMacroBlockByURIResult)
{
  int len = 0;
    LST_DO_CODE(OB_UNIS_ADD_LEN,
          macro_buf_);
  return len;
}


OB_SERIALIZE_MEMBER(ObDelSSTabletMetaArg, tenant_id_, macro_id_);
OB_SERIALIZE_MEMBER(ObEnableSSMicroCacheArg, tenant_id_, is_enabled_);
OB_SERIALIZE_MEMBER(ObGetSSMicroCacheInfoArg, tenant_id_);
OB_SERIALIZE_MEMBER(ObGetSSMicroCacheInfoResult, micro_cache_stat_, super_block_, arc_info_);
OB_SERIALIZE_MEMBER(ObClearSSMicroCacheArg, tenant_id_);
OB_SERIALIZE_MEMBER(ObDelSSLocalTmpFileArg, tenant_id_, macro_id_);
OB_SERIALIZE_MEMBER(ObDelSSLocalMajorArg, tenant_id_);
OB_SERIALIZE_MEMBER(ObCalibrateSSDiskSpaceArg, tenant_id_);
OB_SERIALIZE_MEMBER(ObDelSSTabletMicroArg, tenant_id_, tablet_id_);
OB_SERIALIZE_MEMBER(ObSetSSCkptCompressorArg, tenant_id_, block_type_, compressor_type_);
OB_SERIALIZE_MEMBER(ObSetSSCacheSizeRatioArg, tenant_id_, micro_cache_size_ratio_, macro_cache_size_ratio_);
#endif
#ifdef OB_BUILD_SHARED_STORAGE
OB_SERIALIZE_MEMBER(ObSyncHotMicroKeyArg, tenant_id_, leader_addr_, micro_keys_);
int ObSyncHotMicroKeyArg::assign(const ObSyncHotMicroKeyArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(micro_keys_.assign(other.micro_keys_))) {
    LOG_WARN("micro_keys_ assign failed", KR(ret));
  } else {
    tenant_id_ = other.tenant_id_;
    leader_addr_ = other.leader_addr_;
  }
  return ret;
}

bool ObSyncHotMicroKeyArg::is_valid() const
{
  return is_valid_tenant_id(tenant_id_) && (micro_keys_.count() > 0) && leader_addr_.is_valid();
}
#endif
OB_DEF_SERIALIZE(ObDDLBuildSingleReplicaRequestArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, source_tablet_id_, dest_tablet_id_,
    source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
    parallelism_, tablet_task_id_, data_format_version_,
    dest_schema_version_,
    lob_col_idxs_, is_no_logging_);
  return ret;
}
OB_DEF_DESERIALIZE(ObDDLBuildSingleReplicaRequestArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, source_tablet_id_, dest_tablet_id_,
      source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
      parallelism_, tablet_task_id_, data_format_version_,
      dest_schema_version_,
      lob_col_idxs_, is_no_logging_);
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObDDLBuildSingleReplicaRequestArg)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, source_tablet_id_, dest_tablet_id_,
    source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
    parallelism_, tablet_task_id_, data_format_version_,
    dest_schema_version_,
    lob_col_idxs_, is_no_logging_);
  return len;
}
bool ObDDLBuildSingleReplicaRequestArg::is_valid() const
{
  bool is_valid = source_tablet_id_.is_valid() && dest_tablet_id_.is_valid()
               && OB_INVALID_ID != source_table_id_ && OB_INVALID_ID != dest_schema_id_
               && schema_version_ > 0 && snapshot_version_ > 0 && task_id_ > 0 && parallelism_ > 0
               && tablet_task_id_ > 0 && data_format_version_ > 0
               && dest_schema_version_ > 0;
  return is_valid;
}
int ObDDLBuildSingleReplicaRequestArg::assign(const ObDDLBuildSingleReplicaRequestArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(other));
  } else if (OB_FAIL(lob_col_idxs_.assign(other.lob_col_idxs_))) {
    LOG_WARN("failed to assign to lob col idxs", K(ret));
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
    is_no_logging_ = other.is_no_logging_;
  }
  return ret;
}
OB_SERIALIZE_MEMBER(ObDDLBuildSingleReplicaRequestResult, ret_code_, row_inserted_, row_scanned_, physical_row_count_);
}  // namespace obcall
}  // namespace oceanbase
