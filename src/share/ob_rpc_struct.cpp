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

#include "ob_rpc_struct.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx/ob_trans_service.h"
#include "src/share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace share::schema;
using namespace share;
using namespace storage;
using namespace transaction;
using namespace transaction::tablelock;
using namespace table;
namespace obcall
{
OB_SERIALIZE_MEMBER(Bool, v_);
OB_SERIALIZE_MEMBER(Int64, v_);
OB_SERIALIZE_MEMBER(UInt64, v_);

static const char* upgrade_stage_str[OB_UPGRADE_STAGE_MAX] = {
  "NULL",
  "NONE",
  "PREUPGRADE",
  "DBUPGRADE",
  "POSTUPGRADE"
};

const char* get_upgrade_stage_str(ObUpgradeStage stage)
{
  const char* str = NULL;
  if (stage > OB_UPGRADE_STAGE_INVALID && stage < OB_UPGRADE_STAGE_MAX) {
    str = upgrade_stage_str[stage];
  } else {
    str = upgrade_stage_str[0];
  }
  return str;
}

ObUpgradeStage get_upgrade_stage(const ObString &str)
{
  ObUpgradeStage stage = OB_UPGRADE_STAGE_INVALID;
  for(int64_t i = OB_UPGRADE_STAGE_NONE; i < OB_UPGRADE_STAGE_MAX; i++) {
    if (0 == str.case_compare(upgrade_stage_str[i])) {
      stage = static_cast<ObUpgradeStage>(i);
      break;
    }
  }
  return stage;
}

DEF_TO_STRING(ObServerInfo)
{
  int64_t pos = 0;
  J_KV("zone", zone_,
       "server", server_);
  return pos;
}

OB_SERIALIZE_MEMBER(ObServerInfo,
                    zone_,
                    server_);

DEF_TO_STRING(ObPartitionId)
{
  int64_t pos = 0;
  J_KV(KT_(table_id),
       K_(partition_id));
  return pos;
}

OB_SERIALIZE_MEMBER(ObPartitionId,
                    table_id_,
                    partition_id_);

//////////////////////////////////////////////
// ObClonePartitionArg
//DEF_TO_STRING(ObClonePartitionArg)
//{
//  int64_t pos = 0;
//  J_KV(K_(partition_key),
//       K_(migrate_version),
//       K_(last_sstable_index),
//       K_(last_block_index));
//  return pos;
//}
//
//OB_SERIALIZE_MEMBER(ObClonePartitionArg,
//                                 partition_key_,
//                                 migrate_version_,
//                                 last_sstable_index_,
//                                 last_block_index_);
//
//////////////////////////////////////////////

bool ObStartRedefTableArg::is_valid() const
{
  return (OB_INVALID_ID != orig_table_id_
          && OB_INVALID_ID != target_table_id_
          && share::DDL_INVALID != ddl_type_);
}
int ObStartRedefTableArg::set_nls_formats(const common::ObString *nls_formats)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(nls_formats)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("nls_formats is nullptr", K(ret));
  } else {
    char *tmp_ptr[ObNLSFormatEnum::NLS_MAX] = {};
    for (int64_t i = 0; OB_SUCC(ret) && i < ObNLSFormatEnum::NLS_MAX; ++i) {
      if (OB_ISNULL(tmp_ptr[i] = (char *)allocator_.alloc(nls_formats[i].length()))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory!", K(ret), "size: ", nls_formats[i].length());
      } else {
        MEMCPY(tmp_ptr[i], nls_formats[i].ptr(), nls_formats[i].length());
        nls_formats_[i].assign_ptr(tmp_ptr[i], nls_formats[i].length());
      }
    }
    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; ++i) {
        allocator_.free(tmp_ptr[i]);
      }
    }
  }
  return ret;
}



OB_DEF_SERIALIZE(ObStartRedefTableArg)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE,
          
          orig_table_id_,
          
          target_table_id_,
          session_id_,
          parallelism_,
          ddl_type_,
          ddl_stmt_str_,
          trace_id_,
          sql_mode_,
          tz_info_,
          tz_info_wrap_);
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < ObNLSFormatEnum::NLS_MAX; i++) {
        if (OB_FAIL(nls_formats_[i].serialize(buf, buf_len, pos))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      LST_DO_CODE(OB_UNIS_ENCODE, foreign_key_checks_);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObStartRedefTableArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
          
          orig_table_id_,
          
          target_table_id_,
          session_id_,
          parallelism_,
          ddl_type_,
          ddl_stmt_str_,
          trace_id_,
          sql_mode_,
          tz_info_,
          tz_info_wrap_);
  if (OB_SUCC(ret)) {
    ObString tmp_string;
    char *tmp_ptr[ObNLSFormatEnum::NLS_MAX] = {};
    for (int64_t i = 0; OB_SUCC(ret) && i < ObNLSFormatEnum::NLS_MAX; i++) {
      if (OB_FAIL(tmp_string.deserialize(buf, data_len, pos))) {
      } else if (OB_ISNULL(tmp_ptr[i] = (char *)allocator_.alloc(tmp_string.length()))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory!", K(ret));
      } else {
        MEMCPY(tmp_ptr[i], tmp_string.ptr(), tmp_string.length());
        nls_formats_[i].assign_ptr(tmp_ptr[i], tmp_string.length());
        tmp_string.reset();
      }
    }
    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; i++) {
        allocator_.free(tmp_ptr[i]);
      }
    }
  }
  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_DECODE, foreign_key_checks_);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObStartRedefTableArg)
{
  int64_t len = 0;
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    LST_DO_CODE(OB_UNIS_ADD_LEN,
          
          orig_table_id_,
          
          target_table_id_,
          session_id_,
          parallelism_,
          ddl_type_,
          ddl_stmt_str_,
          trace_id_,
          sql_mode_,
          tz_info_,
          tz_info_wrap_);
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; i++) {
        len += nls_formats_[i].get_serialize_size();
      }
    }
  }
  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, foreign_key_checks_);
  }
  if (OB_FAIL(ret)) {
    len = -1;
  }
  return len;
}

bool ObCopyTableDependentsArg::is_valid() const
{
  return 0 != task_id_;
}


bool ObFinishRedefTableArg::is_valid() const
{
  return (0 != task_id_);
}


bool ObAbortRedefTableArg::is_valid() const
{
  return 0 != task_id_;
}


bool ObUpdateDDLTaskActiveTimeArg::is_valid() const
{
  return 0 != task_id_;
}








bool ObCreateHiddenTableArg::is_valid() const
{
  return (true
          && OB_INVALID_ID != table_id_
          && share::DDL_INVALID != ddl_type_);
}

int ObCreateHiddenTableArg::init(const uint64_t dest_tid,
                                 const uint64_t table_id, const int64_t consumer_group_id, const uint64_t session_id,
                                 const int64_t parallelism, const share::ObDDLType ddl_type, const ObSQLMode sql_mode,
                                 const ObTimeZoneInfo &tz_info, const common::ObString &local_nls_date,
                                 const common::ObString &local_nls_timestamp, const common::ObString &local_nls_timestamp_tz,
                                 const ObTimeZoneInfoWrap &tz_info_wrap, const ObIArray<ObTabletID> &tablet_ids,
                                 const bool need_reorder_column_id, const bool foreign_key_checks)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(tz_info_wrap_.deep_copy(tz_info_wrap))) {
  } else if (FALSE_IT(nls_formats_[ObNLSFormatEnum::NLS_DATE].assign_ptr(local_nls_date.ptr(), static_cast<int32_t>(local_nls_date.length())))) {
    // do nothing
  } else if (FALSE_IT(nls_formats_[ObNLSFormatEnum::NLS_TIMESTAMP].assign_ptr(local_nls_timestamp.ptr(), static_cast<int32_t>(local_nls_timestamp.length())))) {
    // do nothing
  } else if (FALSE_IT(nls_formats_[ObNLSFormatEnum::NLS_TIMESTAMP_TZ].assign_ptr(local_nls_timestamp_tz.ptr(), static_cast<int32_t>(local_nls_timestamp_tz.length())))) {
    // do nothing
  } else if (OB_FAIL(tablet_ids_.assign(tablet_ids))) {
  } else {
    
    
    
    consumer_group_id_ = consumer_group_id;
    table_id_ = table_id;
    parallelism_ = parallelism;
    ddl_type_ = ddl_type;
    session_id_ = session_id;
    sql_mode_ = sql_mode;
    tz_info_ = tz_info;
    // load data no need to reorder column id
    need_reorder_column_id_ = need_reorder_column_id;
    foreign_key_checks_ = foreign_key_checks;
  }
  return ret;
}

OB_DEF_SERIALIZE(ObCreateHiddenTableArg)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(ObDDLArg::serialize(buf, buf_len, pos))) {
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE,
                
                table_id_,
                consumer_group_id_,
                
                session_id_,
                parallelism_,
                ddl_type_,
                sql_mode_,
                tz_info_,
                tz_info_wrap_);
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < ObNLSFormatEnum::NLS_MAX; i++) {
        if (OB_FAIL(nls_formats_[i].serialize(buf, buf_len, pos))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      OB_UNIS_ENCODE(tablet_ids_);
    }
    if (OB_SUCC(ret)) {
      LST_DO_CODE(OB_UNIS_ENCODE, need_reorder_column_id_, foreign_key_checks_);
    }
  }
  return ret;
}
OB_DEF_DESERIALIZE(ObCreateHiddenTableArg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::deserialize(buf, data_len, pos))) {
  } else {
    LST_DO_CODE(OB_UNIS_DECODE,
              
              table_id_,
              consumer_group_id_,
              
              session_id_,
              parallelism_,
              ddl_type_,
              sql_mode_,
              tz_info_,
              tz_info_wrap_);
    ObString tmp_string;
    char *tmp_ptr[ObNLSFormatEnum::NLS_MAX] = {};
    for (int64_t i = 0; OB_SUCC(ret) && i < ObNLSFormatEnum::NLS_MAX; i++) {
      if (OB_FAIL(tmp_string.deserialize(buf, data_len, pos))) {
      } else if (OB_ISNULL(tmp_ptr[i] = (char *)allocator_.alloc(tmp_string.length()))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc memory!", K(ret));
      } else {
        MEMCPY(tmp_ptr[i], tmp_string.ptr(), tmp_string.length());
        nls_formats_[i].assign_ptr(tmp_ptr[i], tmp_string.length());
        tmp_string.reset();
      }
    }
    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; i++) {
        allocator_.free(tmp_ptr[i]);
      }
    }
    if (OB_SUCC(ret)) {
      OB_UNIS_DECODE(tablet_ids_);
    }
    if (OB_SUCC(ret)) {
      LST_DO_CODE(OB_UNIS_DECODE, need_reorder_column_id_, foreign_key_checks_);
    }
  }
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObCreateHiddenTableArg)
{
  int64_t len = 0;
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    len += ObDDLArg::get_serialize_size();
    LST_DO_CODE(OB_UNIS_ADD_LEN,
                
                table_id_,
                consumer_group_id_,
                
                session_id_,
                parallelism_,
                ddl_type_,
                sql_mode_,
                tz_info_,
                tz_info_wrap_);
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; i++) {
        len += nls_formats_[i].get_serialize_size();
      }
    }
    if (OB_SUCC(ret)) {
      OB_UNIS_ADD_LEN(tablet_ids_);
    }
    if (OB_SUCC(ret)) {
      LST_DO_CODE(OB_UNIS_ADD_LEN, need_reorder_column_id_, foreign_key_checks_);
    }
  }
  if (OB_FAIL(ret)) {
    len = -1;
  }
  return len;
}


OB_SERIALIZE_MEMBER(ObCreateHiddenTableRes,
                    
                    table_id_,
                    
                    dest_table_id_,
                    trace_id_,
                    task_id_,
                    schema_version_,
                    is_no_logging_);

OB_SERIALIZE_MEMBER(ObStartRedefTableRes,
                    task_id_,
                    
                    schema_version_);

OB_SERIALIZE_MEMBER(ObCopyTableDependentsArg,
                    task_id_,
                    
                    copy_indexes_,
                    copy_triggers_,
                    copy_constraints_,
                    copy_foreign_keys_,
                    ignore_errors_);

OB_SERIALIZE_MEMBER(ObFinishRedefTableArg,
                    task_id_);

OB_SERIALIZE_MEMBER(ObAbortRedefTableArg,
                    task_id_);

OB_SERIALIZE_MEMBER(ObUpdateDDLTaskActiveTimeArg,
                    task_id_);







//////////////////////////////////////////////
//
//  Tenant
//
//////////////////////////////////////////////


DEF_TO_STRING(ObSysVarIdValue)
{
  int64_t pos = 0;
  J_KV(K_(sys_id),
       K_(value));
  return pos;
}

OB_SERIALIZE_MEMBER(ObSysVarIdValue, sys_id_, value_);

bool ObCreateTenantArg::is_valid() const
{
  return !tenant_schema_.get_tenant_name_str().empty() && pool_list_.count() > 0
         && (!is_restore_ || (is_restore_ && palf_base_info_.is_valid()
                              && recovery_until_scn_.is_valid_and_not_min()
                              && compatible_version_ > 0))
         && (!is_creating_standby_ || (is_creating_standby_ && !log_restore_source_.empty()));
}

ObTenantRole ObCreateTenantArg::get_tenant_role() const
{
  ObTenantRole role;
  if (is_restore_tenant()) {
    role = share::RESTORE_TENANT_ROLE;
  } else if (is_standby_tenant()) {
    role = share::STANDBY_TENANT_ROLE;
  } else {
    role = share::PRIMARY_TENANT_ROLE;
  }
  return role;
}


int ObCreateTenantArg::assign(const ObCreateTenantArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::assign(other))) {
  } else if (OB_FAIL(tenant_schema_.assign(other.tenant_schema_))) {
  } else if (OB_FAIL(pool_list_.assign(other.pool_list_))) {
  } else if (OB_FAIL(sys_var_list_.assign(other.sys_var_list_))) {
  } else {
    if_not_exist_ = other.if_not_exist_;
    name_case_mode_ = other.name_case_mode_;
    is_restore_ = other.is_restore_;
    palf_base_info_ = other.palf_base_info_;
    recovery_until_scn_ = other.recovery_until_scn_;
    compatible_version_ = other.compatible_version_;
    is_creating_standby_ = other.is_creating_standby_;
    log_restore_source_ = other.log_restore_source_;
    is_tmp_tenant_for_recover_ = other.is_tmp_tenant_for_recover_;
  }
  return ret;
}

void ObCreateTenantArg::reset()
{
  ObDDLArg::reset();
  tenant_schema_.reset();
  pool_list_.reset();
  if_not_exist_ = false;
  sys_var_list_.reset();
  name_case_mode_ = common::OB_NAME_CASE_INVALID;
  is_restore_ = false;
  palf_base_info_.reset();
  compatible_version_ = 0;
  is_creating_standby_ = false;
  log_restore_source_.reset();
  is_tmp_tenant_for_recover_ = false;
}






int ObLoadTenantTableSchemaArg::init(const uint64_t table_id,
    const ObIArray<share::ObLoadInnerTableSchemaInfo> *schema_infos,
    const ObIArray<int64_t> &insert_idx, const uint64_t data_version)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(insert_idx_.assign(insert_idx))) {
  } else {
    
    table_id_ = table_id;
    data_version_ = data_version;
    schema_infos_ = reinterpret_cast<uint64_t>(schema_infos);
  }
  return ret;
}

int ObLoadTenantTableSchemaArg::assign(const ObLoadTenantTableSchemaArg &arg)
{
  int ret = OB_SUCCESS;
  if (this == &arg) {
  } else if (OB_FAIL(insert_idx_.assign(arg.insert_idx_))) {
  } else {
    
    table_id_ = arg.table_id_;
    data_version_ = arg.data_version_;
    schema_infos_ = arg.schema_infos_;
  }
  return ret;
}

bool ObLoadTenantTableSchemaArg::is_valid() const
{
  bool valid = true;
  if (table_id_ > OB_MAX_INNER_TABLE_ID) {
    valid = false;
  } else if (insert_idx_.count() <= 0) {
    valid = false;
  } else if (data_version_ != DATA_CURRENT_VERSION) {
    valid = false;
  }
  return valid;
}

OB_SERIALIZE_MEMBER(ObLoadTenantTableSchemaArg, table_id_, data_version_, schema_infos_, insert_idx_);

DEF_TO_STRING(ObCreateTenantArg)
{
  int64_t pos = 0;
  J_KV(K_(tenant_schema),
       K_(pool_list),
       K_(if_not_exist),
       K_(sys_var_list),
       K_(name_case_mode),
       K_(is_restore),
       K_(palf_base_info),
       K_(recovery_until_scn),
       K_(compatible_version),
       K_(is_creating_standby),
       K_(log_restore_source),
       K_(is_tmp_tenant_for_recover));
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateTenantArg, ObDDLArg),
                    tenant_schema_,
                    pool_list_,
                    if_not_exist_,
                    sys_var_list_,
                    name_case_mode_,
                    is_restore_,
                    palf_base_info_,
                    compatible_version_,
                    recovery_until_scn_,
                    is_creating_standby_,
                    log_restore_source_,
                    is_tmp_tenant_for_recover_);



int ObAddSysVarArg::init(const bool &update_sys_var, const bool &if_not_exist,
    const share::schema::ObSysVarSchema &sysvar)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sysvar_.assign(sysvar))) {
  } else {
    
    update_sys_var_ = update_sys_var;
    if_not_exist_ = if_not_exist;
    is_batch_ = false;
    sysvars_.reset();
  }
  return ret;
}


bool ObAddSysVarArg::is_valid() const
{
  bool valid = true;
  if (!is_batch_) {
    valid = sysvar_.is_valid();
  } else {
    FOREACH_X(it, sysvars_, valid) {
      valid = it->is_valid();
    }
  }
  return valid;
}


OB_SERIALIZE_MEMBER((ObAddSysVarArg, ObDDLArg), sysvar_, if_not_exist_, update_sys_var_,
    is_batch_, sysvars_);

DEF_TO_STRING(ObAddSysVarArg)
{
  int64_t pos = 0;
  J_KV(K_(sysvar), K_(if_not_exist), K_(update_sys_var), K_(is_batch), K_(sysvars));
  return pos;
}

bool ObModifySysVarArg::is_valid() const
{
  return !sys_var_list_.empty();
}


OB_SERIALIZE_MEMBER((ObModifySysVarArg, ObDDLArg), sys_var_list_, is_inner_);

DEF_TO_STRING(ObModifySysVarArg)
{
  int64_t pos = 0;
  J_KV(K_(sys_var_list), K_(is_inner));
  return pos;
}

bool ObCreateDatabaseArg::is_valid() const
{
  return !database_schema_.get_database_name_str().empty();
}

DEF_TO_STRING(ObCreateDatabaseArg)
{
  int64_t pos = 0;
  J_KV(K_(database_schema),
       K_(if_not_exist));
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateDatabaseArg, ObDDLArg),
                    database_schema_,
                    if_not_exist_);

bool ObAlterDatabaseArg::is_valid() const
{
  return !database_schema_.get_database_name_str().empty();
}

DEF_TO_STRING(ObAlterDatabaseArg)
{
  int64_t pos = 0;
  J_KV(K_(database_schema));
  return pos;
}

OB_SERIALIZE_MEMBER((ObAlterDatabaseArg, ObDDLArg),
                    database_schema_,
                    alter_option_bitset_);

bool ObDropDatabaseArg::is_valid() const
{
  return !database_name_.empty()
    && lib::Worker::CompatMode::INVALID != compat_mode_;
}

DEF_TO_STRING(ObDropDatabaseArg)
{
  int64_t pos = 0;
  J_KV(
       K_(database_name),
       K_(if_exist),
       K_(to_recyclebin),
       K_(is_add_to_scheduler),
       K_(compat_mode));
  return pos;
}

OB_SERIALIZE_MEMBER((ObDropDatabaseArg, ObDDLArg),
                    
                    database_name_,
                    if_exist_,
                    to_recyclebin_,
                    is_add_to_scheduler_,
                    compat_mode_);

bool ObCreateTablegroupArg::is_valid() const
{
  return !tablegroup_schema_.get_tablegroup_name().empty();
}

DEF_TO_STRING(ObCreateTablegroupArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tablegroup_schema),
       K_(if_not_exist));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateTablegroupArg, ObDDLArg),
                    tablegroup_schema_,
                    if_not_exist_)

bool ObDropTablegroupArg::is_valid() const
{
  return !tablegroup_name_.empty();
}

DEF_TO_STRING(ObDropTablegroupArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(tablegroup_name),
       K_(if_exist));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObDropTablegroupArg, ObDDLArg),
                    
                    tablegroup_name_,
                    if_exist_);

bool ObAlterTablegroupArg::is_valid() const
{
  return !tablegroup_name_.empty();
}

bool ObAlterTablegroupArg::is_alter_partitions() const
{
  return alter_option_bitset_.has_member(ADD_PARTITION)
         || alter_option_bitset_.has_member(DROP_PARTITION)
         || alter_option_bitset_.has_member(PARTITIONED_TABLE)
         || alter_option_bitset_.has_member(REORGANIZE_PARTITION)
         || alter_option_bitset_.has_member(SPLIT_PARTITION);
}

bool ObAlterTablegroupArg::is_allow_when_disable_ddl() const
{
  bool bret = false;
  if (alter_option_bitset_.is_empty()) {
    bret = false;
  } else {
    bret = true;
    for (int64_t i = 0; i < MAX_OPTION && bret; i++) {
      if (alter_option_bitset_.has_member(i) && i != PRIMARY_ZONE) {
        bret = false;
      }
    }
  }
  return bret;
}

bool ObAlterTablegroupArg::is_allow_when_upgrade() const
{
  bool bret = false;
  if (alter_option_bitset_.is_empty()) {
    bret = false;
  } else {
    bret = true;
    for (int64_t i = 0; i < MAX_OPTION && bret; i++) {
      if (alter_option_bitset_.has_member(i)
          && i != PRIMARY_ZONE
          && i != LOCALITY
          && i != FORCE_LOCALITY) {
        bret = false;
      }
    }
  }
  return bret;
}

DEF_TO_STRING(ObAlterTablegroupArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(table_items),
       
       K_(tablegroup_name),
       K_(alter_tablegroup_schema));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObAlterTablegroupArg, ObDDLArg),
                    
                    tablegroup_name_,
                    table_items_,
                    alter_option_bitset_,
                    alter_tablegroup_schema_);


DEF_TO_STRING(ObCreateVertialPartitionArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(vertical_partition_columns));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateVertialPartitionArg, ObDDLArg),
                    vertical_partition_columns_);

bool ObCreateTableArg::is_valid() const
{
  // index_arg_list can be empty
  return !schema_.get_table_name_str().empty();
}

int ObCreateTableArg::assign(const ObCreateTableArg &other)
{
  int ret = OB_SUCCESS;
  OZ(ObDDLArg::assign(other));
  OX(if_not_exist_ = other.if_not_exist_);
  OZ(schema_.assign(other.schema_));
  OZ(index_arg_list_.assign(other.index_arg_list_));
  OZ(foreign_key_arg_list_.assign(other.foreign_key_arg_list_));
  OZ(constraint_list_.assign(other.constraint_list_));
  OX(db_name_ = other.db_name_);
  OX(last_replay_log_id_ = other.last_replay_log_id_);
  OX(is_inner_ = other.is_inner_);
  OZ(vertical_partition_arg_list_.assign(other.vertical_partition_arg_list_));
  OZ(error_info_.assign(other.error_info_));
  OX(is_alter_view_ = other.is_alter_view_);
  OZ(sequence_ddl_arg_.assign(other.sequence_ddl_arg_));
  OZ(dep_infos_.assign(other.dep_infos_));
  OZ(mv_ainfo_.assign(other.mv_ainfo_));

  return ret;
}

DEF_TO_STRING(ObCreateTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(if_not_exist),
       K_(schema),
       K_(index_arg_list),
       K_(constraint_list),
       K_(db_name),
       K_(last_replay_log_id),
       K_(foreign_key_arg_list),
       K_(is_inner),
       K_(vertical_partition_arg_list),
       K_(error_info),
       K_(is_alter_view),
       K_(sequence_ddl_arg),
       K_(dep_infos));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateTableArg, ObDDLArg),
                    if_not_exist_,
                    schema_,
                    index_arg_list_,
                    db_name_,
                    foreign_key_arg_list_,
                    constraint_list_,
                    last_replay_log_id_,
                    is_inner_,
                    vertical_partition_arg_list_,
                    error_info_,
                    is_alter_view_,
                    sequence_ddl_arg_,
                    dep_infos_,
                    mv_ainfo_);

bool ObCreateTableArg::is_allow_when_upgrade() const
{
  bool bret = true;
  if (0 != foreign_key_arg_list_.count()
      || 0 != vertical_partition_arg_list_.count()) {
    bret = false;
  } else {
    for (int64_t i = 0; bret && i < constraint_list_.count(); i++) {
      if (CONSTRAINT_TYPE_PRIMARY_KEY != constraint_list_.at(i).get_constraint_type()) {
        bret = false;
      }
    }
  }
  return bret;
}

OB_SERIALIZE_MEMBER(ObCreateTableRes, table_id_, schema_version_, task_id_, do_nothing_);

OB_SERIALIZE_MEMBER(ObDropTableRes, schema_version_, task_id_, do_nothing_);

bool ObCreateTableLikeArg::is_valid() const
{
  return !origin_db_name_.empty()
      && !origin_table_name_.empty() && !new_db_name_.empty()
      && !new_table_name_.empty()
      && (table_type_ == USER_TABLE || table_type_ == TMP_TABLE);
}


DEF_TO_STRING(ObCreateTableLikeArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(if_not_exist),
       
       K_(origin_db_name),
       K_(origin_table_name),
       K_(new_db_name),
       K_(new_table_name),
       K_(table_type),
       K_(create_host),
       K_(sequence_ddl_arg),
       K_(session_id),
       K_(define_user_id));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateTableLikeArg, ObDDLArg),
                    if_not_exist_,
                    
                    origin_db_name_,
                    origin_table_name_,
                    new_db_name_,
                    new_table_name_,
                    table_type_,
                    create_host_,
                    sequence_ddl_arg_,
                    session_id_,
                    define_user_id_);



bool ObSetCommentArg::is_valid() const
{
  return !database_name_.empty()
         && !table_name_.empty()
         && op_type_ > MIN_OP_TYPE
         && op_type_ < MAX_OP_TYPE;
}

OB_SERIALIZE_MEMBER((ObSetCommentArg, ObDDLArg),
                     session_id_,
                     database_name_,
                     table_name_,
                     column_name_list_,
                     column_comment_list_,
                     table_comment_,
                     op_type_);

bool ObAlterTableArg::is_valid() const
{
  // TODO(shaohang.lsh): add more check if needed
  if (is_refresh_sess_active_time()) {
    return true;
  } else {
    return !alter_table_schema_.origin_database_name_.empty()
        && !alter_table_schema_.origin_table_name_.empty();
  }
}

bool ObAlterTableArg::is_refresh_sess_active_time() const
{
  return (alter_table_schema_.alter_option_bitset_.has_member(SESSION_ACTIVE_TIME)
          && OB_DDL_ALTER_TABLE == alter_table_schema_.alter_type_
          && OB_INVALID_ID != session_id_);
}

bool ObAlterTableArg::is_allow_when_disable_ddl() const
{
  bool bret = false;
  if (alter_table_schema_.alter_option_bitset_.is_empty()) {
    bret = false;
  } else {
    bret = true;
    for (int64_t i = 0; i < MAX_OPTION && bret && is_alter_options_; i++) {
      if (alter_table_schema_.alter_option_bitset_.has_member(i) && i != PRIMARY_ZONE) {
        bret = false;
      }
    }
  }
  return bret;
}

bool ObAlterTableArg::is_allow_when_upgrade() const
{
  bool bret = false;
  if (alter_table_schema_.alter_option_bitset_.is_empty()
      && !is_alter_columns_
      && !is_alter_indexs_) {
    bret = false;
  } else {
    bret = true;
    if (is_alter_indexs_) {
      for (int64_t i = 0 ; bret && i < index_arg_list_.count(); i++) {
        if (OB_ISNULL(index_arg_list_.at(i))) {
          bret = false;
          LOG_WARN_RET(OB_ERR_UNEXPECTED, "ptr is null", K(bret));
        } else {
          bret = index_arg_list_.at(i)->is_allow_when_upgrade();
        }
      }
    }
    for (int64_t i = 0; i < MAX_OPTION && bret && is_alter_options_; i++) {
      if (alter_table_schema_.alter_option_bitset_.has_member(i)
          && i != PRIMARY_ZONE
          && i != LOCALITY
          && i != FORCE_LOCALITY) {
        bret = false;
      }
    }
    if (is_alter_columns_ && bret) {
      // Only add columns and extend the length of the columns will be allowed again in ddl_service
      ObTableSchema::const_column_iterator it_begin = alter_table_schema_.column_begin();
      ObTableSchema::const_column_iterator it_end = alter_table_schema_.column_end();
      AlterColumnSchema *alter_column_schema = NULL;
      for(; bret && it_begin != it_end; it_begin++) {
        if (OB_ISNULL(*it_begin)) {
          bret = false;
          LOG_WARN_RET(OB_ERR_UNEXPECTED, "*it_begin is NULL", K(bret));
        } else {
          alter_column_schema = static_cast<AlterColumnSchema *>(*it_begin);
          // mysql mode, OB_ALL_MODIFY_COLUMN function is a subset of OB_ALL_CHANGE_COLUMN;
          // Oracle mode, only OB_ALL_MODIFY_COLUMN. In the case of only supporting extended column length, for simplicity of implementation, only OB_ALL_MODIFY_COLUMN is left here.
          if (OB_DDL_MODIFY_COLUMN != alter_column_schema->alter_type_
              && OB_DDL_ADD_COLUMN != alter_column_schema->alter_type_) {
            bret = false;
          }
        }
      }
    }
  }
  return bret;
}

int ObAlterTableArg::is_alter_comment(bool &is_alter_comment) const
{
  int ret = OB_SUCCESS;
  is_alter_comment = alter_table_schema_.alter_option_bitset_.has_member(COMMENT);
  if (!is_alter_comment && is_alter_columns_) {
    ObTableSchema::const_column_iterator it_begin = alter_table_schema_.column_begin();
    ObTableSchema::const_column_iterator it_end = alter_table_schema_.column_end();
    AlterColumnSchema *alter_column_schema = NULL;
    for (; OB_SUCC(ret) && !is_alter_comment && it_begin != it_end; it_begin++) {
      if (OB_ISNULL(alter_column_schema = static_cast<AlterColumnSchema *>(*it_begin))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("alter_column_schema is NULL", K(ret));
      } else {
        is_alter_comment |= alter_column_schema->is_set_comment_;
      }
    }
  }
  return ret;
}

int ObAlterTableArg::set_nls_formats(const common::ObString *nls_formats)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(nls_formats)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    char *tmp_ptr[ObNLSFormatEnum::NLS_MAX] = {};
    for (int64_t i = 0; OB_SUCC(ret) && i < ObNLSFormatEnum::NLS_MAX; ++i) {
      if (OB_ISNULL(tmp_ptr[i] = (char *)allocator_.alloc(nls_formats[i].length()))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        SHARE_LOG(ERROR, "failed to alloc memory!", "size", nls_formats[i].length(), K(ret));
      } else {
        MEMCPY(tmp_ptr[i], nls_formats[i].ptr(), nls_formats[i].length());
        nls_formats_[i].assign_ptr(tmp_ptr[i], nls_formats[i].length());
      }
    }
    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; ++i) {
        allocator_.free(tmp_ptr[i]);
      }
    }
  }
  return ret;
}

int ObAlterTableArg::serialize_index_args(char *buf, const int64_t data_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (!is_valid() || NULL == buf || data_len <= 0 || pos >= data_len) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "self", *this, KP(buf), K(data_len), K(pos));
  } else if (OB_FAIL(serialization::encode_vi64(buf, data_len, pos, index_arg_list_.size()))) {
  }
  for (int i = 0; OB_SUCC(ret) && i < index_arg_list_.size(); ++i) {
    ObIndexArg *index_arg = index_arg_list_.at(i);
    if (index_arg->index_action_type_ == ObIndexArg::ALTER_PRIMARY_KEY
      || index_arg->index_action_type_ == ObIndexArg::DROP_PRIMARY_KEY) {
      ObAlterPrimaryArg *alter_pk_arg = static_cast<ObAlterPrimaryArg *>(index_arg);
      if (NULL == alter_pk_arg) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
          alter_pk_arg->index_action_type_))) {
      } else if (OB_FAIL(alter_pk_arg->serialize(buf, data_len, pos))) {
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::ADD_INDEX
              || index_arg->index_action_type_ == ObIndexArg::ADD_PRIMARY_KEY) {
      ObCreateIndexArg *create_index_arg = static_cast<ObCreateIndexArg *>(index_arg);
      if (NULL == create_index_arg) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
          create_index_arg->index_action_type_))) {
      } else if (OB_FAIL(create_index_arg->serialize(buf, data_len, pos))) {
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::DROP_INDEX) {
      ObDropIndexArg *drop_index_arg = static_cast<ObDropIndexArg *>(index_arg);
      if (NULL == drop_index_arg) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    drop_index_arg->index_action_type_))) {
      } else if (OB_FAIL(drop_index_arg->serialize(buf, data_len, pos))) {
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::ALTER_INDEX) {
      ObAlterIndexArg *alter_index_arg = static_cast<ObAlterIndexArg *>(index_arg);
      if (OB_UNLIKELY(NULL == alter_index_arg)) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    alter_index_arg->index_action_type_))) {
      } else if (OB_FAIL(alter_index_arg->serialize(buf, data_len, pos))) {
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::ALTER_INDEX_PARALLEL) {
      ObAlterIndexParallelArg *alter_index_parallel_arg = static_cast<ObAlterIndexParallelArg *>(index_arg);
      if (OB_UNLIKELY(NULL == alter_index_parallel_arg)) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    alter_index_parallel_arg->index_action_type_))) {
      } else if (OB_FAIL(alter_index_parallel_arg->serialize(buf, data_len, pos))) {
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::RENAME_INDEX) {
      ObRenameIndexArg *rename_index_arg = static_cast<ObRenameIndexArg *>(index_arg);
      SHARE_LOG(WARN, "serialize rename index arg!", K(rename_index_arg->origin_index_name_), K(rename_index_arg->new_index_name_));

      if (OB_UNLIKELY(NULL == rename_index_arg)) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    rename_index_arg->index_action_type_))) {
      } else if (OB_FAIL(rename_index_arg->serialize(buf, data_len, pos))) {
      }
    } else if (index_arg->index_action_type_ == ObIndexArg::DROP_FOREIGN_KEY) {
      ObDropForeignKeyArg *foreign_key_arg = static_cast<ObDropForeignKeyArg *>(index_arg);
      if (OB_UNLIKELY(NULL == foreign_key_arg)) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "index arg is null", K(ret));
      } else if (OB_FAIL(serialization::encode_vi32(buf, data_len, pos,
                                                    foreign_key_arg->index_action_type_))) {
      } else if (OB_FAIL(foreign_key_arg->serialize(buf, data_len, pos))) {
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      SHARE_LOG(WARN, "unknown index action type", K_(index_arg->index_action_type), K(ret));
    }
  }
  return ret;
}

int ObAlterTableArg::alloc_index_arg(const ObIndexArg::IndexActionType index_action_type, ObIndexArg *&index_arg)
{
  int ret = OB_SUCCESS;
  void *tmp_ptr = nullptr;
  if (index_action_type == ObIndexArg::ALTER_PRIMARY_KEY
    || index_action_type == ObIndexArg::DROP_PRIMARY_KEY) {
    ObAlterPrimaryArg *alter_pk_arg = NULL;
    if (NULL == (tmp_ptr = allocator_.alloc(sizeof(ObAlterPrimaryArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to alloc memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObAlterPrimaryArg();
    }
  } else if (index_action_type == ObIndexArg::ADD_INDEX
            || index_action_type == ObIndexArg::ADD_PRIMARY_KEY) {
    ObCreateIndexArg *create_index_arg = NULL;
    if (NULL == (tmp_ptr = allocator_.alloc(sizeof(ObCreateIndexArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to alloc memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObCreateIndexArg();
    }
  } else if (index_action_type == ObIndexArg::DROP_INDEX) {
    ObDropIndexArg *drop_index_arg = NULL;
    if (NULL == (tmp_ptr = (ObDropIndexArg *)allocator_.alloc(sizeof(ObDropIndexArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObDropIndexArg();
    }
  } else if (index_action_type == ObIndexArg::ALTER_INDEX) {
    ObAlterIndexArg *alter_index_arg = NULL;
    if (OB_UNLIKELY(NULL == (tmp_ptr = (ObAlterIndexArg *)allocator_.alloc(sizeof(ObAlterIndexArg))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObAlterIndexArg();
    }
  } else if (index_action_type == ObIndexArg::ALTER_INDEX_PARALLEL) {
    ObAlterIndexParallelArg *alter_index_parallel_arg = NULL;
    if (OB_UNLIKELY(NULL == (tmp_ptr = (ObAlterIndexParallelArg *)allocator_.alloc(sizeof(ObAlterIndexParallelArg))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObAlterIndexParallelArg();
    }
  } else if (index_action_type == ObIndexArg::RENAME_INDEX) {
    ObRenameIndexArg *rename_index_arg = NULL;
    if (OB_UNLIKELY(NULL == (tmp_ptr = (ObRenameIndexArg *)allocator_.alloc(sizeof(ObRenameIndexArg))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObRenameIndexArg();
    }
  } else if (index_action_type == ObIndexArg::DROP_FOREIGN_KEY) {
    ObDropForeignKeyArg *drop_foreign_key_arg = NULL;
    if (OB_UNLIKELY(NULL == (tmp_ptr = (ObDropForeignKeyArg *)allocator_.alloc(sizeof(ObDropForeignKeyArg))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SHARE_LOG(ERROR, "failed to allocate memory!", K(ret));
    } else {
      index_arg = new (tmp_ptr) ObDropForeignKeyArg();
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "unknown index action type", K(index_action_type), K(ret));
  }
  return ret;
}

int ObAlterTableArg::deserialize_index_args(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0) || OB_UNLIKELY(pos > data_len)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("buf should not be null", K(buf), K(data_len), K(pos), K(ret));
  } else if (pos == data_len) {
    //do nothing
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
  }
  for (int i = 0; OB_SUCC(ret) && i < count; ++i) {
    ObIndexArg::IndexActionType index_action_type = ObIndexArg::INVALID_ACTION;
    ObIndexArg *index_arg = nullptr;
    if (OB_FAIL(serialization::decode_vi32(buf, data_len, pos, ((int32_t *)(&index_action_type))))) {
      SHARE_LOG(WARN, "failed to decode index action type", K(ret));
      break;
    } else if (OB_FAIL(alloc_index_arg(index_action_type, index_arg))) {
    } else if (OB_ISNULL(index_arg)) {
      ret = OB_ERR_UNEXPECTED;
      SHARE_LOG(WARN, "error unexpected, index arg must not be nullptr", K(ret));
    } else if (OB_FAIL(index_arg->deserialize(buf, data_len, pos))) {
    } else if (OB_FAIL(index_arg_list_.push_back(index_arg))) {
    }
    if (OB_FAIL(ret) && nullptr != index_arg) {
      index_arg->~ObIndexArg();
      allocator_.free(index_arg);
      index_arg = nullptr;
    }
  }
  return ret;
}

int64_t ObAlterTableArg::get_index_args_serialize_size() const
{
  int ret = OB_SUCCESS;
  int64_t len = 0;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "self", *this);
  } else {
    len += serialization::encoded_length_vi64(index_arg_list_.size());
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < index_arg_list_.size(); ++i) {
    ObIndexArg *index_arg = index_arg_list_.at(i);
    if (NULL == index_arg) {
      ret = OB_INVALID_ARGUMENT;
      SHARE_LOG(WARN, "index arg should not be null", K(ret));
    } else {
      len += serialization::encoded_length(index_arg->index_action_type_);
      if (ObIndexArg::DROP_PRIMARY_KEY == index_arg->index_action_type_
        || ObIndexArg::ALTER_PRIMARY_KEY == index_arg->index_action_type_) {
        ObAlterPrimaryArg *alter_pk_arg = static_cast<ObAlterPrimaryArg *>(index_arg);
        if (NULL == alter_pk_arg) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += alter_pk_arg->get_serialize_size();
        }
      } else if (ObIndexArg::ADD_INDEX == index_arg->index_action_type_
                || ObIndexArg::ADD_PRIMARY_KEY == index_arg->index_action_type_) {
        ObCreateIndexArg *create_index_arg = static_cast<ObCreateIndexArg *>(index_arg);
        if (NULL == create_index_arg) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += create_index_arg->get_serialize_size();
        }
      } else if (ObIndexArg::DROP_INDEX == index_arg->index_action_type_) {
        ObDropIndexArg *drop_index_arg = static_cast<ObDropIndexArg *>(index_arg);
        if (NULL == drop_index_arg) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += drop_index_arg->get_serialize_size();
        }
      } else if (ObIndexArg::ALTER_INDEX == index_arg->index_action_type_) {
        ObAlterIndexArg *alter_index_arg = static_cast<ObAlterIndexArg *>(index_arg);
        if (OB_UNLIKELY(NULL == alter_index_arg)) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += alter_index_arg->get_serialize_size();
        }
      } else if (ObIndexArg::DROP_FOREIGN_KEY == index_arg->index_action_type_) {
        ObDropForeignKeyArg *drop_foreign_key_arg = static_cast<ObDropForeignKeyArg *>(index_arg);
        if (OB_UNLIKELY(NULL == drop_foreign_key_arg)) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "foreign key arg is null", K(ret));
        } else {
          len += drop_foreign_key_arg->get_serialize_size();
        }
      } else if (ObIndexArg::ALTER_INDEX_PARALLEL == index_arg->index_action_type_) {
        ObAlterIndexParallelArg *alter_index_parallel_arg =
          static_cast<ObAlterIndexParallelArg *>(index_arg);
        if (OB_UNLIKELY(NULL == alter_index_parallel_arg)) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += alter_index_parallel_arg->get_serialize_size();
        }
      } else if (ObIndexArg::RENAME_INDEX == index_arg->index_action_type_) {
        ObRenameIndexArg *rename_index_arg = static_cast<ObRenameIndexArg *>(index_arg);
        if (OB_UNLIKELY(NULL == rename_index_arg)) {
          ret = OB_INVALID_ARGUMENT;
          SHARE_LOG(WARN, "index arg is null", K(ret));
        } else {
          len += rename_index_arg->get_serialize_size();
        }
      } else {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "Invalid index action type!", K(ret));
      }
    }
  }
  if (OB_FAIL(ret)) {
    len = -1;
  }
  return len;
}

OB_DEF_SERIALIZE(ObAlterTableArg)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "self", *this);
  } else if (OB_FAIL(ObDDLArg::serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialize_index_args(buf, buf_len, pos))) {
  } else if (OB_FAIL(alter_table_schema_.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(tz_info_.serialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialization::encode_vi32(buf, buf_len, pos, alter_part_type_))) {
  } else if (OB_FAIL(serialization::encode_vi32(buf, buf_len, pos, alter_constraint_type_))) {
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, session_id_))) {
  } else if (OB_FAIL(tz_info_wrap_.serialize(buf, buf_len, pos))) {
  } else {
    for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; ++i) {
      if (OB_FAIL(nls_formats_[i].serialize(buf, buf_len, pos))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(foreign_key_arg_list_.serialize(buf, buf_len, pos))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(sequence_ddl_arg_.serialize(buf, buf_len, pos))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, sql_mode_))) {
    }
  }
  LST_DO_CODE(OB_UNIS_ENCODE,
              ddl_task_type_,
              compat_mode_,
              table_id_,
              hidden_table_id_,
              is_alter_columns_,
              is_alter_indexs_,
              is_alter_options_,
              is_alter_partitions_,
              is_inner_,
              is_update_global_indexes_,
              is_convert_to_character_,
              skip_sys_table_check_,
              need_rebuild_trigger_,
              foreign_key_checks_,
              is_add_to_scheduler_,
              inner_sql_exec_addr_,
              local_session_var_,
              mview_refresh_info_,
              alter_algorithm_,
              alter_auto_partition_attr_,
              rebuild_index_arg_list_,
              client_session_id_,
              client_session_create_ts_,
              lock_priority_,
              is_direct_load_partition_,
              is_alter_column_group_delayed_);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(rebuild_index_arg_list_.serialize(buf, buf_len, pos))) {
    }
  }

  LST_DO_CODE(OB_UNIS_ENCODE,
              is_alter_mview_attributes_,
              alter_mview_arg_,
              is_alter_mlog_attributes_,
              alter_mlog_arg_,
              part_storage_cache_policy_,
              data_version_);

  return ret;
}

OB_DEF_DESERIALIZE(ObAlterTableArg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::deserialize(buf, data_len, pos))) {
  } else if (OB_FAIL(deserialize_index_args(buf, data_len, pos))) {
  } else if (OB_FAIL(alter_table_schema_.deserialize(buf, data_len, pos))) {
  } else if (OB_FAIL(tz_info_.deserialize(buf, data_len, pos))) {
  } else if (OB_FAIL(serialization::decode_vi32(buf, data_len, pos, ((int32_t *)(&alter_part_type_))))) {
  } else if (OB_FAIL(serialization::decode_vi32(buf, data_len, pos, ((int32_t *)(&alter_constraint_type_))))) {
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, ((int64_t *)(&session_id_))))) {
  } else if (pos < data_len) {
    if (OB_FAIL(tz_info_wrap_.deserialize(buf, data_len, pos))) {
    }
  } else {
    tz_info_wrap_.set_tz_info_offset(tz_info_.get_offset());
    tz_info_wrap_.set_error_on_overlap_time(tz_info_.is_error_on_overlap_time());
  }

  if (OB_SUCC(ret) && pos < data_len) {
    ObString tmp_string;
    char *tmp_ptr[ObNLSFormatEnum::NLS_MAX] = {};
    for (int64_t i = 0; OB_SUCC(ret) && i < ObNLSFormatEnum::NLS_MAX; ++i) {
      if (OB_FAIL(tmp_string.deserialize(buf, data_len, pos))) {
      } else if (OB_ISNULL(tmp_ptr[i] = (char *)allocator_.alloc(tmp_string.length()))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        SHARE_LOG(ERROR, "failed to alloc memory!", "size", tmp_string.length(), K(ret));
      } else {
        MEMCPY(tmp_ptr[i], tmp_string.ptr(), tmp_string.length());
        nls_formats_[i].assign_ptr(tmp_ptr[i], tmp_string.length());
        tmp_string.reset();
      }
    }
    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; ++i) {
        allocator_.free(tmp_ptr[i]);
      }
    }
  } else {
    nls_formats_[ObNLSFormatEnum::NLS_DATE] = ObTimeConverter::COMPAT_OLD_NLS_DATE_FORMAT;
    nls_formats_[ObNLSFormatEnum::NLS_TIMESTAMP] = ObTimeConverter::COMPAT_OLD_NLS_TIMESTAMP_FORMAT;
    nls_formats_[ObNLSFormatEnum::NLS_TIMESTAMP_TZ] = ObTimeConverter::COMPAT_OLD_NLS_TIMESTAMP_TZ_FORMAT;
  }

  if (OB_SUCC(ret) && pos < data_len) {
    if (OB_FAIL(foreign_key_arg_list_.deserialize(buf, data_len, pos))) {
    }
  }

  if (OB_SUCC(ret) && pos < data_len) {
    if (OB_FAIL(sequence_ddl_arg_.deserialize(buf, data_len, pos))) {
    }
  }
  if (OB_SUCC(ret) && pos < data_len) {
    if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, reinterpret_cast<int64_t *>(&sql_mode_)))) {
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE,
              ddl_task_type_,
              compat_mode_,
              table_id_,
              hidden_table_id_,
              is_alter_columns_,
              is_alter_indexs_,
              is_alter_options_,
              is_alter_partitions_,
              is_inner_,
              is_update_global_indexes_,
              is_convert_to_character_,
              skip_sys_table_check_,
              need_rebuild_trigger_,
              foreign_key_checks_,
              is_add_to_scheduler_,
              inner_sql_exec_addr_,
              local_session_var_,
              mview_refresh_info_,
              alter_algorithm_,
              alter_auto_partition_attr_,
              rebuild_index_arg_list_,
              client_session_id_,
              client_session_create_ts_,
              lock_priority_,
              is_direct_load_partition_,
              is_alter_column_group_delayed_);

  if (OB_SUCC(ret) && pos < data_len) {
    if (OB_FAIL(rebuild_index_arg_list_.deserialize(buf, data_len, pos))) {
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE,
              is_alter_mview_attributes_,
              alter_mview_arg_,
              is_alter_mlog_attributes_,
              alter_mlog_arg_,
              part_storage_cache_policy_,
              data_version_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObAlterTableArg)
{
  int64_t len = 0;
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "self", *this);
  } else  {
    len += ObDDLArg::get_serialize_size();
    len += get_index_args_serialize_size();
    len += alter_table_schema_.get_serialize_size();
    len += tz_info_.get_serialize_size();
    len += serialization::encoded_length_vi32(alter_part_type_);
    len += serialization::encoded_length_vi32(alter_constraint_type_);
    len += serialization::encoded_length_vi64(session_id_);
    len += tz_info_wrap_.get_serialize_size();
    for (int64_t i = 0; i < ObNLSFormatEnum::NLS_MAX; ++i) {
      len += nls_formats_[i].get_serialize_size();
    }
    len += foreign_key_arg_list_.get_serialize_size();
    len += rebuild_index_arg_list_.get_serialize_size();
    len += sequence_ddl_arg_.get_serialize_size();
    len += serialization::encoded_length_i64(sql_mode_);
    LST_DO_CODE(OB_UNIS_ADD_LEN,
                ddl_task_type_,
                compat_mode_,
                table_id_,
                hidden_table_id_,
                is_alter_columns_,
                is_alter_indexs_,
                is_alter_options_,
                is_alter_partitions_,
                is_inner_,
                is_update_global_indexes_,
                is_convert_to_character_,
                skip_sys_table_check_,
                need_rebuild_trigger_,
                foreign_key_checks_,
                is_add_to_scheduler_,
                inner_sql_exec_addr_,
                local_session_var_,
                mview_refresh_info_,
                alter_algorithm_,
                alter_auto_partition_attr_,
                rebuild_index_arg_list_,
                client_session_id_,
                client_session_create_ts_,
                lock_priority_,
                is_direct_load_partition_,
                is_alter_column_group_delayed_,
                is_alter_mview_attributes_,
                alter_mview_arg_,
                is_alter_mlog_attributes_,
                alter_mlog_arg_,
                part_storage_cache_policy_,
                data_version_);
  }

  if (OB_FAIL(ret)) {
    len = -1;
  }
  return len;
}

bool ObExchangePartitionArg::is_valid() const
{
  return OB_INVALID_ID != session_id_ && PARTITION_LEVEL_ZERO != exchange_partition_level_ && PARTITION_LEVEL_MAX != exchange_partition_level_ && OB_INVALID_ID != base_table_id_
         && !base_table_part_name_.empty() && OB_INVALID_ID != inc_table_id_;
}

int ObExchangePartitionArg::assign(const ObExchangePartitionArg &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
    //do nothing
  } else if (OB_FAIL(ObDDLArg::assign(other))) {
  } else {
    session_id_ = other.session_id_;
    
    exchange_partition_level_ = other.exchange_partition_level_;
    base_table_id_ = other.base_table_id_;
    base_table_part_name_ = other.base_table_part_name_;
    inc_table_id_ = other.inc_table_id_;
    including_indexes_ = other.including_indexes_;
    without_validation_ = other.without_validation_;
    update_global_indexes_ = other.update_global_indexes_;
  }
  return ret;
}

OB_DEF_SERIALIZE(ObExchangePartitionArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,
              session_id_,
              
              exchange_partition_level_,
              base_table_id_,
              base_table_part_name_,
              inc_table_id_,
              including_indexes_,
              without_validation_,
              update_global_indexes_);
  return ret;
}

OB_DEF_DESERIALIZE(ObExchangePartitionArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,
              session_id_,
              
              exchange_partition_level_,
              base_table_id_,
              base_table_part_name_,
              inc_table_id_,
              including_indexes_,
              without_validation_,
              update_global_indexes_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObExchangePartitionArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              session_id_,
              
              exchange_partition_level_,
              base_table_id_,
              base_table_part_name_,
              inc_table_id_,
              including_indexes_,
              without_validation_,
              update_global_indexes_);
  return len;
}

DEF_TO_STRING(ObExchangePartitionArg)
{
  int64_t pos = 0;
  pos += ObDDLArg::to_string(buf + pos, buf_len - pos);
  J_OBJ_START();
  J_KV(K_(session_id),
       
       K_(exchange_partition_level),
       K_(base_table_id),
       K_(base_table_part_name),
       K_(inc_table_id),
       K_(including_indexes),
       K_(without_validation),
       K_(update_global_indexes));
  J_OBJ_END();
  return pos;
}

bool ObTruncateTableArg::is_valid() const
{
  return !database_name_.empty()
      && !table_name_.empty() && lib::Worker::CompatMode::INVALID != compat_mode_;
}

OB_SERIALIZE_MEMBER((ObTruncateTableArg, ObDDLArg),
                    
                    database_name_,
                    table_name_,
                    session_id_,
                    is_add_to_scheduler_,
                    compat_mode_,
                    foreign_key_checks_);

DEF_TO_STRING(ObTruncateTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(database_name),
       K_(table_name),
       K_(session_id),
       K_(is_add_to_scheduler),
       K_(compat_mode),
       K_(foreign_key_checks));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObRenameTableItem, origin_db_name_,
                                       new_db_name_,
                                       origin_table_name_,
                                       new_table_name_);

DEF_TO_STRING(ObRenameTableItem)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(origin_db_name),
       K_(new_db_name),
       K_(origin_table_name),
       K_(new_table_name),
       K_(origin_table_id));
  J_OBJ_END();
  return pos;
}

bool ObRenameTableItem::is_valid() const
{
  return !origin_db_name_.empty() && !new_db_name_.empty()
      && !origin_table_name_.empty() && !new_table_name_.empty();
}

bool ObRenameTableArg::is_valid() const
{
  return rename_table_items_.count() > 0;
}

DEF_TO_STRING(ObRenameTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(rename_table_items),
       K_(client_session_id),
       K_(client_session_create_ts),
       K_(lock_priority));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObRenameTableArg, ObDDLArg),
                    
                    rename_table_items_,
                    client_session_id_,
                    client_session_create_ts_,
                    lock_priority_);

DEF_TO_STRING(ObTableItem)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(database_name),
       K_(table_name),
       K_(is_hidden),
       K_(table_id));
  J_OBJ_END();
  return pos;
}

bool ObTableItem::operator==(const ObTableItem &r) const
{
  bool ret = false;
  if (OB_NAME_CASE_INVALID != mode_ && mode_ == r.mode_) {
    if (!table_name_.empty() && !r.table_name_.empty() &&
        !database_name_.empty() && !r.database_name_.empty()) {
      //todo compare using case mode @hualong
      //ret = ObCharset::case_mode_equal(mode_, table_name_, r.table_name_) &&
      //    ObCharset::case_mode_equal(mode_, database_name_, r.database_name_);
      ret = table_name_ == r.table_name_ && database_name_ == r.database_name_ && is_hidden_ == r.is_hidden_
        && table_id_ == r.table_id_;
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTableItem,
                    database_name_,
                    table_name_,
                    is_hidden_,
                    table_id_);

bool ObDropTableArg::is_valid() const
{
  bool ret = (table_type_ < MAX_TABLE_TYPE
              && tables_.count() > 0);
  if (false == ret && (TMP_TABLE == table_type_ || TMP_TABLE_ALL == table_type_)) {
    LOG_WARN("drop table valid check for temp table");
    ret = (session_id_ != OB_INVALID_ID && true == if_exist_ && false == to_recyclebin_ && lib::Worker::CompatMode::INVALID != compat_mode_);
  }
  return ret;
}


DEF_TO_STRING(ObDropTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(table_type),
       K_(tables),
       K_(if_exist),
       K_(to_recyclebin),
       K_(session_id),
       K_(sess_create_time),
       K_(foreign_key_checks),
       K_(is_add_to_scheduler),
       K_(force_drop),
       K_(compat_mode));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObDropTableArg, ObDDLArg),
                    
                    table_type_,
                    tables_,
                    if_exist_,
                    to_recyclebin_,
                    session_id_,
                    sess_create_time_,
                    foreign_key_checks_,
                    is_add_to_scheduler_,
                    force_drop_,
                    compat_mode_);

int ObForkTableArg::assign(const ObForkTableArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::assign(other))) {
  } else {
    
    src_database_name_ = other.src_database_name_;
    src_table_name_ = other.src_table_name_;
    dst_database_name_ = other.dst_database_name_;
    dst_table_name_ = other.dst_table_name_;
    if_not_exist_ = other.if_not_exist_;
    session_id_ = other.session_id_;
  }
  return ret;
}

bool ObForkTableArg::is_valid() const
{
  return (!src_database_name_.empty()
          && !src_table_name_.empty()
          && !dst_database_name_.empty()
          && !dst_table_name_.empty());
}

DEF_TO_STRING(ObForkTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(src_database_name),
       K_(src_table_name),
       K_(dst_database_name),
       K_(dst_table_name),
       K_(if_not_exist),
       K_(session_id));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObForkTableArg, ObDDLArg),
                    
                    src_database_name_,
                    src_table_name_,
                    dst_database_name_,
                    dst_table_name_,
                    if_not_exist_,
                    session_id_);

int ObForkDatabaseArg::assign(const ObForkDatabaseArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::assign(other))) {
  } else {
    
    src_database_name_ = other.src_database_name_;
    dst_database_name_ = other.dst_database_name_;
    if_not_exist_ = other.if_not_exist_;
    session_id_ = other.session_id_;
  }
  return ret;
}

bool ObForkDatabaseArg::is_valid() const
{
  return (!src_database_name_.empty()
          && !dst_database_name_.empty());
}

DEF_TO_STRING(ObForkDatabaseArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(src_database_name),
       K_(dst_database_name),
       K_(if_not_exist),
       K_(session_id));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObForkDatabaseArg, ObDDLArg),
                    
                    src_database_name_,
                    dst_database_name_,
                    if_not_exist_,
                    session_id_);

bool ObOptimizeTableArg::is_valid() const
{
  return (tables_.count() > 0);
}

OB_SERIALIZE_MEMBER((ObOptimizeTableArg, ObDDLArg),
    tables_);

DEF_TO_STRING(ObOptimizeTableArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tables));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObOptimizeTenantArg, ObDDLArg), tenant_name_);

bool ObOptimizeTenantArg::is_valid() const
{
  return !tenant_name_.empty();
}

DEF_TO_STRING(ObOptimizeTenantArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tenant_name));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObOptimizeAllArg, ObDDLArg));

DEF_TO_STRING(ObOptimizeAllArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_OBJ_END();
  return pos;
}

DEF_TO_STRING(ObColumnSortItem)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(column_name),
       K_(prefix_len),
       K_(order_type),
       K_(column_id),
       K_(is_func_index));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObColumnSortItem,
                                 column_name_,
                                 prefix_len_,
                                 order_type_,
                                 column_id_,
                                 is_func_index_);


bool ObIndexOption::is_valid() const
{
  // if replica_num not set, it's default value is zero
  return block_size_ > 0
      && index_status_ > INDEX_STATUS_NOT_FOUND
      && index_status_ < INDEX_STATUS_MAX
      && progressive_merge_num_ >= 0;
}

DEF_TO_STRING(ObTableOption)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(block_size),
       K_(replica_num),
       K_(index_status),
       K_(use_bloom_filter),
       K_(compress_method),
       K_(comment),
       K_(tablegroup_name),
       K_(progressive_merge_num),
       K_(primary_zone),
       K_(row_store_type),
       K_(store_format),
       K_(enable_macro_block_bloom_filter),
       K_(storage_cache_policy));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER(ObTableOption,
                    block_size_,
                    replica_num_,
                    index_status_,
                    use_bloom_filter_,
                    compress_method_,
                    comment_,
                    progressive_merge_num_,
                    row_store_type_,
                    store_format_,
                    enable_macro_block_bloom_filter_,
                    storage_cache_policy_);

DEF_TO_STRING(ObIndexOption)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(block_size),
       K_(replica_num),
       K_(index_status),
       K_(use_bloom_filter),
       K_(compress_method),
       K_(comment),
       K_(tablegroup_name),
       K_(progressive_merge_num),
       K_(primary_zone),
       K_(parser_name),
       K_(parser_properties),
       K_(index_attributes_set),
       K_(row_store_type),
       K_(store_format),
       K_(enable_macro_block_bloom_filter),
       K_(storage_cache_policy));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObIndexOption, ObTableOption), parser_name_, index_attributes_set_, parser_properties_);

bool ObIndexArg::is_valid() const
{
  return !index_name_.empty() && !table_name_.empty()
      && !database_name_.empty() && INVALID_ACTION != index_action_type_;
}

bool ObIndexArg::is_allow_when_upgrade() const
{
  return ADD_INDEX == index_action_type_
         || DROP_INDEX == index_action_type_
         || DROP_FOREIGN_KEY == index_action_type_;
}

DEF_TO_STRING(ObIndexArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(session_id),
       K_(index_name),
       K_(table_name),
       K_(database_name),
       K_(index_action_type),
       K_(compact_level),
       K_(storage_cache_policy));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObIndexArg, ObDDLArg),
                    
                    index_name_,
                    table_name_,
                    database_name_,
                    index_action_type_,
                    session_id_,
                    compact_level_,
                    storage_cache_policy_);

bool ObCreateIndexArg::is_valid() const
{
  // store_columns_ can be empty
  return ObIndexArg::is_valid() && index_type_ > INDEX_TYPE_IS_NOT
         && index_type_ < INDEX_TYPE_MAX
         && index_columns_.count() > 0
         && index_option_.is_valid()
         && index_using_type_ >= USING_BTREE
         && index_using_type_ < USING_TYPE_MAX;
}
OB_SERIALIZE_MEMBER(ObCreateIndexArg::ObIndexColumnGroupItem, is_each_cg_, column_list_, cg_type_);

int ObCreateIndexArg::ObIndexColumnGroupItem::assign(const ObCreateIndexArg::ObIndexColumnGroupItem &other)
{
  int ret = OB_SUCCESS;
  is_each_cg_ = other.is_each_cg_;
  cg_type_ = other.cg_type_;
  if (OB_FAIL(column_list_.assign(other.column_list_))) {
  }
  return ret;
}

DEF_TO_STRING(ObCreateIndexArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_INDEX_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(index_type),
       K_(index_columns),
       K_(fulltext_columns),
       K_(store_columns),
       K_(index_option),
       K_(index_using_type),
       K_(data_table_id),
       K_(index_table_id),
       K_(if_not_exist),
       K_(index_schema),
       K_(is_inner),
       K_(nls_date_format),
       K_(nls_timestamp_format),
       K_(nls_timestamp_tz_format),
       K_(sql_mode),
       K_(inner_sql_exec_addr),
       K_(local_session_var),
       K_(exist_all_column_group),
       K_(index_cgs),
       K_(vidx_refresh_info),
       K_(is_rebuild_index),
       K_(is_index_scope_specified),
       K_(is_offline_rebuild),
       K_(index_key),
       K_(data_version));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateIndexArg, ObIndexArg),
                    index_type_,
                    index_columns_,
                    store_columns_,
                    index_option_,
                    index_using_type_,
                    fulltext_columns_,
                    data_table_id_,
                    index_table_id_,
                    if_not_exist_,
                    with_rowid_,
                    index_schema_,
                    is_inner_,
                    hidden_store_columns_,
                    nls_date_format_,
                    nls_timestamp_format_,
                    nls_timestamp_tz_format_,
                    sql_mode_,
                    inner_sql_exec_addr_,
                    local_session_var_,
                    exist_all_column_group_,
                    index_cgs_,
                    vidx_refresh_info_,
                    is_rebuild_index_,
                    is_index_scope_specified_,
                    is_offline_rebuild_,
                    index_key_,
                    data_version_);



OB_SERIALIZE_MEMBER((ObCreateAuxIndexArg, ObDDLArg),
                    
                    data_table_id_,
                    create_index_arg_,
                    snapshot_version_);
OB_SERIALIZE_MEMBER(ObCreateAuxIndexRes,
                    aux_table_id_,
                    ddl_task_id_,
                    schema_generated_);


DEF_TO_STRING(ObAlterIndexArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_INDEX_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(index_visibility));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObAlterIndexArg, ObIndexArg), index_visibility_);
OB_SERIALIZE_MEMBER((ObDropLobArg, ObDDLArg), session_id_, data_table_id_, aux_lob_meta_table_id_);

DEF_TO_STRING(ObDropIndexArg) {
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(index_name),
       K_(table_name),
       K_(database_name),
       K_(index_action_type),
       K_(index_table_id),
       K_(is_add_to_scheduler),
       K_(is_in_recyclebin),
       K_(is_hidden),
       K_(is_inner),
       K_(is_vec_inner_drop),
       K_(only_set_status),
       K_(index_ids),
       K_(table_id),
       K_(is_drop_in_rebuild_task));
  J_OBJ_END();
  return pos;
}
OB_SERIALIZE_MEMBER((ObDropIndexArg, ObIndexArg),
                    
                    index_name_,
                    table_name_,
                    database_name_,
                    index_action_type_,
                    index_table_id_,
                    is_add_to_scheduler_,
                    is_in_recyclebin_,
                    is_hidden_,
                    is_inner_,
                    is_vec_inner_drop_,
                    only_set_status_,
                    index_ids_,
                    is_parent_task_dropping_fts_index_,
                    is_parent_task_dropping_multivalue_index_,
                    table_id_,
                    is_drop_in_rebuild_task_,
                    is_parent_task_dropping_spiv_index_);

OB_SERIALIZE_MEMBER(ObDropIndexRes, index_table_id_, schema_version_, task_id_);

int ObDropIndexArg::assign(const ObDropIndexArg &other)
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(ObIndexArg::assign(other))) {
  } else if (OB_FAIL(index_ids_.assign(other.index_ids_))) {
  } else {
    index_table_id_ = other.index_table_id_;
    is_add_to_scheduler_ = other.is_add_to_scheduler_;
    is_hidden_ = other.is_hidden_;
    is_in_recyclebin_ = other.is_in_recyclebin_;
    is_inner_ = other.is_inner_;
    is_vec_inner_drop_ = other.is_vec_inner_drop_;
    is_parent_task_dropping_fts_index_ = other.is_parent_task_dropping_fts_index_;
    is_parent_task_dropping_multivalue_index_ = other.is_parent_task_dropping_multivalue_index_;
    only_set_status_ = other.only_set_status_;
    table_id_ = other.table_id_;
    is_drop_in_rebuild_task_ = other.is_drop_in_rebuild_task_;
    is_parent_task_dropping_spiv_index_ = other.is_parent_task_dropping_spiv_index_;
  }
  return ret;
}


DEF_TO_STRING(ObRebuildIndexArg) {
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(
       K_(index_name),
       K_(table_name),
       K_(database_name),
       K_(index_action_type),
       K_(index_table_id),
       K_(vidx_refresh_info),
       K_(rebuild_index_type),
       K_(create_mlog_arg));
  J_OBJ_END();
  return pos;
}
OB_SERIALIZE_MEMBER((ObRebuildIndexArg, ObIndexArg),
                    index_table_id_,
                    vidx_refresh_info_,
                    rebuild_index_type_,
                    create_mlog_arg_);


DEF_TO_STRING(ObAlterIndexParallelArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_INDEX_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(new_parallel));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObAlterIndexParallelArg, ObIndexArg), new_parallel_);

DEF_TO_STRING(ObRenameIndexArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_INDEX_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(origin_index_name),
       K_(new_index_name));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObRenameIndexArg, ObIndexArg), origin_index_name_, new_index_name_);


DEF_TO_STRING(ObCreateForeignKeyArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_FOREIGN_KEY_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(parent_database),
       K_(parent_table),
       K_(child_columns),
       K_(parent_columns),
       K_(update_action),
       K_(delete_action),
       K_(foreign_key_name),
       K_(enable_flag),
       K_(is_modify_enable_flag),
       K_(fk_ref_type),
       K_(ref_cst_id),
       K_(validate_flag),
       K_(is_modify_validate_flag),
       K_(rely_flag),
       K_(is_modify_rely_flag),
       K_(is_modify_fk_state),
       K_(parent_database_id),
       K_(parent_table_id),
       K_(name_generated_type));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateForeignKeyArg, ObIndexArg),
                    parent_database_,
                    parent_table_,
                    child_columns_,
                    parent_columns_,
                    update_action_,
                    delete_action_,
                    foreign_key_name_,
                    enable_flag_,
                    is_modify_enable_flag_,
                    fk_ref_type_, // FARM COMPAT WHITELIST for ref_cst_type_
                    ref_cst_id_,
                    validate_flag_,
                    is_modify_validate_flag_,
                    rely_flag_,
                    is_modify_rely_flag_,
                    is_modify_fk_state_,
                    need_validate_data_,
                    is_parent_table_mock_,
                    parent_database_id_,
                    parent_table_id_,
                    name_generated_type_);


DEF_TO_STRING(ObDropForeignKeyArg)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME(N_FOREIGN_KEY_ARG);
  J_COLON();
  pos += ObIndexArg::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(foreign_key_name));
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER((ObDropForeignKeyArg, ObIndexArg),
                    foreign_key_name_);

bool ObFlashBackTableFromRecyclebinArg::is_valid() const
{
  int bret = true;
  if (origin_table_name_.empty()) {
    bret = false;
    LOG_WARN_RET(OB_INVALID_ERROR, "origin_table_name is empty", K_(origin_table_name));
  } else if ((new_db_name_.empty() && !new_table_name_.empty()) ||
      (!new_db_name_.empty() && new_table_name_.empty())) {
    bret = false;
    LOG_WARN_RET(OB_INVALID_ERROR, "new_db_name or new_table_name is invalid",
             K_(new_db_name), K_(new_table_name));
  }
  return bret;
}

OB_SERIALIZE_MEMBER((ObFlashBackTableFromRecyclebinArg, ObDDLArg),
                    
                    origin_table_name_,
                    new_db_name_,
                    new_table_name_,
                    origin_db_name_);

bool ObFlashBackTableToScnArg::is_valid() const
{
  int bret = true;
  if (OB_INVALID_ID == time_point_) {
    bret = false;
    LOG_WARN_RET(OB_INVALID_ERROR, "timepoint is invalid", K_(time_point));
  } else if (0 == tables_.count()) {
    bret = false;
    LOG_WARN_RET(OB_INVALID_ERROR, "table is empty", K_(tables));
  } else if (-1 == query_end_time_) {
    bret = false;
  }
  return bret;
}

OB_SERIALIZE_MEMBER(ObFlashBackTableToScnArg,
                    
                    time_point_,
                    tables_,
                    query_end_time_);

bool ObFlashBackIndexArg::is_valid() const
{
  int bret = true;
  if (origin_table_name_.empty()) {
    bret = false;
    LOG_WARN_RET(OB_INVALID_ERROR, "origin_table_name is empty", K_(origin_table_name));
  } else if ((new_db_name_.empty() && !new_table_name_.empty()) ||
      (!new_db_name_.empty() && new_table_name_.empty())) {
    bret = false;
    LOG_WARN_RET(OB_INVALID_ERROR, "new_db_name or new_table_name is invalid",
             K_(new_db_name), K_(new_table_name));
  }
  return bret;
}

OB_SERIALIZE_MEMBER((ObFlashBackIndexArg, ObDDLArg),
                    
                    origin_table_name_,
                    new_db_name_,
                    new_table_name_);

bool ObPurgeIndexArg::is_valid() const
{
  return OB_INVALID_ID != database_id_ && !table_name_.empty();
}



OB_SERIALIZE_MEMBER((ObPurgeIndexArg, ObDDLArg),
                    
                    database_id_,
                    table_name_);

bool ObFlashBackDatabaseArg::is_valid() const
{
  return !origin_db_name_.empty();
}

OB_SERIALIZE_MEMBER((ObFlashBackDatabaseArg, ObDDLArg),
                    
                    origin_db_name_,
                    new_db_name_);

bool ObPurgeTableArg::is_valid() const
{
  return OB_INVALID_ID != database_id_ && !table_name_.empty();
}

OB_SERIALIZE_MEMBER((ObPurgeTableArg, ObDDLArg),
                    
                    database_id_,
                    table_name_);

bool ObPurgeDatabaseArg::is_valid() const
{
  return !db_name_.empty();
}

OB_SERIALIZE_MEMBER((ObPurgeDatabaseArg, ObDDLArg),
                    
                    db_name_);





OB_SERIALIZE_MEMBER((ObPurgeRecycleBinArg, ObDDLArg),
                    
                    purge_num_,
                    expire_time_,
                    auto_purge_);


OB_SERIALIZE_MEMBER((ObDependencyObjDDLArg, ObDDLArg),
                    
                    insert_dep_objs_,
                    update_dep_objs_,
                    delete_dep_objs_,
                    schema_,
                    reset_view_column_infos_);


OB_SERIALIZE_MEMBER(ObCheckFrozenScnArg, frozen_scn_);
OB_SERIALIZE_MEMBER(ObGetMinSSTableSchemaVersionArg, batch_id_arg_list_);
OB_SERIALIZE_MEMBER(ObGetMinSSTableSchemaVersionRes, ret_list_);

ObCheckFrozenScnArg::ObCheckFrozenScnArg()
{
  frozen_scn_.set_min();
}

bool ObCheckFrozenScnArg::is_valid() const
{
  return frozen_scn_.is_valid() && frozen_scn_ > SCN::min_scn();
}





DEF_TO_STRING(ObCreateTabletBatchInTransRes)
{
  int64_t pos = 0;
  J_KV(K_(ret), K_(tx_result));
  return pos;
}

OB_SERIALIZE_MEMBER(ObCreateTabletBatchInTransRes, ret_, tx_result_);




OB_SERIALIZE_MEMBER(ObCalcColumnChecksumRequestArg::SingleItem, ls_id_, tablet_id_, calc_table_id_);

bool ObCalcColumnChecksumRequestArg::SingleItem::is_valid() const
{
  return ls_id_.is_valid() && tablet_id_.is_valid() && OB_INVALID_ID != calc_table_id_;
}

void ObCalcColumnChecksumRequestArg::SingleItem::reset()
{
  ls_id_.reset();
  tablet_id_.reset();
  calc_table_id_ = OB_INVALID_ID;
}

int ObCalcColumnChecksumRequestArg::SingleItem::assign(const SingleItem &other)
{
  int ret = OB_SUCCESS;
  ls_id_ = other.ls_id_;
  tablet_id_ = other.tablet_id_;
  calc_table_id_ = other.calc_table_id_;
  return ret;
}

OB_SERIALIZE_MEMBER(
    ObCalcColumnChecksumRequestArg,
    
    target_table_id_,
    schema_version_,
    execution_id_,
    snapshot_version_,
    source_table_id_,
    task_id_,
    calc_items_,
    user_parallelism_);

bool ObCalcColumnChecksumRequestArg::is_valid() const
{
  bool bret = OB_INVALID_ID != target_table_id_
      && OB_INVALID_VERSION != schema_version_ && execution_id_ >= 0
      && OB_INVALID_VERSION != snapshot_version_ && OB_INVALID_ID != source_table_id_
      && task_id_ > 0;
  for (int64_t i = 0; bret && i < calc_items_.count(); ++i) {
    bret = calc_items_.at(i).is_valid();
  }
  return bret;
}

void ObCalcColumnChecksumRequestArg::reset()
{
  
  target_table_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  snapshot_version_ = OB_INVALID_VERSION;
  source_table_id_ = OB_INVALID_ID;
  execution_id_ = -1;
  task_id_ = 0;
  user_parallelism_ = 0;
}

OB_SERIALIZE_MEMBER(ObCalcColumnChecksumRequestRes, ret_codes_);

OB_SERIALIZE_MEMBER(
    ObCalcColumnChecksumResponseArg,
    tablet_id_,
    target_table_id_,
    ret_code_,
    source_table_id_,
    schema_version_,
    task_id_);

bool ObCalcColumnChecksumResponseArg::is_valid() const
{
  return tablet_id_.is_valid()
      && OB_INVALID_ID != target_table_id_
      && OB_INVALID_ID != source_table_id_
      && schema_version_ > 0
      && task_id_ > 0
      && true;
}

void ObCalcColumnChecksumResponseArg::reset()
{
  tablet_id_.reset();
  target_table_id_ = OB_INVALID_ID;
  ret_code_ = OB_SUCCESS;
  source_table_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  task_id_ = 0;
  
}

//----End structs for partition online/offline----


DEF_TO_STRING(ObSwitchSchemaArg)
{
  int64_t pos = 0;
  J_KV(K_(schema_info),
       K_(force_refresh),
       K_(is_async));
  return pos;
}

OB_SERIALIZE_MEMBER(ObSwitchSchemaArg, schema_info_, force_refresh_, is_async_);



OB_SERIALIZE_MEMBER(ObLSTabletPair, ls_id_, tablet_id_);
OB_SERIALIZE_MEMBER(ObCheckSchemaVersionElapsedArg, schema_version_, need_wait_trans_end_, tablets_, ddl_task_id_);

bool ObCheckSchemaVersionElapsedArg::is_valid() const
{
  bool bret = schema_version_ > 0 && !tablets_.empty() && ddl_task_id_ >= 0;
  for (int64_t i = 0; bret && i < tablets_.count(); ++i) {
    bret = tablets_.at(i).is_valid();
  }
  return bret;
}


bool ObCheckModifyTimeElapsedArg::is_valid() const
{
  bool bret = sstable_exist_ts_ > 0 && ddl_task_id_ >= 0;
  for (int64_t i = 0; bret && i < tablets_.count(); ++i) {
    bret = tablets_.at(i).is_valid();
  }
  return bret;
}

int ObDDLCheckTabletMergeStatusArg::assign(const ObDDLCheckTabletMergeStatusArg &other) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(tablet_ids_.assign(other.tablet_ids_))) {
  } else {
    
    ls_id_ = other.ls_id_;
    snapshot_version_ = other.snapshot_version_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObDDLCheckTabletMergeStatusArg, ls_id_, tablet_ids_, snapshot_version_);


OB_SERIALIZE_MEMBER(ObCheckModifyTimeElapsedArg, sstable_exist_ts_, tablets_, ddl_task_id_);


OB_SERIALIZE_MEMBER(ObCheckTransElapsedResult, ret_code_, snapshot_, pending_tx_id_);
OB_SERIALIZE_MEMBER(ObCheckSchemaVersionElapsedResult, results_);


OB_SERIALIZE_MEMBER(CandidateStatus, candidate_status_);

OB_SERIALIZE_MEMBER(ObDDLCheckTabletMergeStatusResult, merge_status_);

//----Structs for managing privileges----
OB_SERIALIZE_MEMBER(ObAccountArg,
                    user_name_,
                    host_name_,
                    is_role_);

bool ObSchemaReviseArg::is_valid() const
{
  bool bret = false;
  if (REVISE_CONSTRAINT_COLUMN_INFO == type_
      || REVISE_NOT_NULL_CONSTRAINT == type_) {
    bret = (OB_INVALID_ID != table_id_)
           && !(REVISE_CONSTRAINT_COLUMN_INFO == type_ && 0 == csts_array_.count());
  }
  return bret;
}


OB_SERIALIZE_MEMBER((ObSchemaReviseArg, ObDDLArg),
                    type_,
                    
                    table_id_,
                    csts_array_);

bool ObCreateUserArg::is_valid() const
{
  return user_infos_.count() > 0;
}


OB_SERIALIZE_MEMBER((ObCreateUserArg, ObDDLArg),
                    
                    user_infos_,
                    if_not_exist_,
                    creator_id_,
                    primary_zone_,
                    is_create_role_);

bool ObDropUserArg::is_valid() const
{
  return users_.count() > 0 && hosts_.count() == users_.count();
}

OB_DEF_SERIALIZE(ObDropUserArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,
              
              users_,
              hosts_,
              is_role_);
  return ret;
}

OB_DEF_DESERIALIZE(ObDropUserArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,
              
              users_,
              hosts_);

  //compatibility for old version
  if (OB_SUCC(ret) && users_.count() > 0 && hosts_.empty()) {
    const ObString TMP_DEFAULT_HOST_NAME(OB_DEFAULT_HOST_NAME);
    for (int64_t i = 0; i < users_.count() && OB_SUCC(ret); ++i) {
      if (OB_FAIL(hosts_.push_back(TMP_DEFAULT_HOST_NAME))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_DECODE, is_role_);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDropUserArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              
              users_,
              hosts_,
              is_role_);
  return len;
}

bool ObAlterRoleArg::is_valid() const
{
  return role_name_.length() > 0;
}


OB_DEF_SERIALIZE(ObAlterRoleArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,
              
              role_name_,
              host_name_,
              pwd_enc_);
  return ret;
}

OB_DEF_DESERIALIZE(ObAlterRoleArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,
              
              role_name_,
              host_name_,
              pwd_enc_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObAlterRoleArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              
              role_name_,
              host_name_,
              pwd_enc_);
  return len;
}

bool ObRenameUserArg::is_valid() const
{
  return (old_users_.count() > 0
          && old_users_.count() == new_users_.count()
          && old_hosts_.count() == new_hosts_.count()
          && old_users_.count() == old_hosts_.count());
}

OB_DEF_SERIALIZE(ObRenameUserArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,
              
              old_users_,
              new_users_,
              old_hosts_,
              new_hosts_);
  return ret;
}

OB_DEF_DESERIALIZE(ObRenameUserArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,
              
              old_users_,
              new_users_,
              old_hosts_,
              new_hosts_);

  //compatibility for old version
  if (OB_SUCC(ret)
      && old_users_.count() > 0
      && new_users_.count() == old_users_.count()
      && (old_hosts_.empty() || new_hosts_.empty())) {
    if (old_hosts_.empty() != new_hosts_.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("old_hosts and new_hosts should have same count", K_(old_hosts), K_(new_hosts), K(ret));
    } else {
      const ObString TMP_DEFAULT_HOST_NAME(OB_DEFAULT_HOST_NAME);
      for (int64_t i = 0; i < old_users_.count() && OB_SUCC(ret); ++i) {
        if (OB_FAIL(old_hosts_.push_back(TMP_DEFAULT_HOST_NAME))) {
        } else if (OB_FAIL(new_hosts_.push_back(TMP_DEFAULT_HOST_NAME))) {
        }
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRenameUserArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              
              old_users_,
              new_users_,
              old_hosts_,
              new_hosts_);
  return len;
}


bool ObSetPasswdArg::is_valid() const
{
  // user_name_ and passwd_ can be empty
  return true;
}

OB_DEF_SERIALIZE(ObSetPasswdArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,
              
              user_,
              passwd_,
              host_,
              ssl_type_,
              ssl_cipher_,
              x509_issuer_,
              x509_subject_,
              modify_max_connections_,
              max_connections_per_hour_,
              max_user_connections_);
  return ret;
}

OB_DEF_DESERIALIZE(ObSetPasswdArg)
{
  int ret = OB_SUCCESS;
  host_.assign_ptr(OB_DEFAULT_HOST_NAME, static_cast<int32_t>(STRLEN(OB_DEFAULT_HOST_NAME)));
  ssl_type_ = schema::ObSSLType::SSL_TYPE_NOT_SPECIFIED;
  ssl_cipher_.reset();
  x509_issuer_.reset();
  x509_subject_.reset();

  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,
              
              user_,
              passwd_,
              host_,
              ssl_type_,
              ssl_cipher_,
              x509_issuer_,
              x509_subject_,
              modify_max_connections_,
              max_connections_per_hour_,
              max_user_connections_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSetPasswdArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              
              user_,
              passwd_,
              host_,
              ssl_type_,
              ssl_cipher_,
              x509_issuer_,
              x509_subject_,
              modify_max_connections_,
              max_connections_per_hour_,
              max_user_connections_);
  return len;
}

bool ObLockUserArg::is_valid() const
{
  return users_.count() > 0 && users_.count() == hosts_.count();
}

OB_DEF_SERIALIZE(ObLockUserArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,
              
              users_,
              locked_,
              hosts_);
  return ret;
}

OB_DEF_DESERIALIZE(ObLockUserArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,
              
              users_,
              locked_,
              hosts_);

  //compatibility for old version
  if (OB_SUCC(ret) && users_.count() > 0 && hosts_.empty()) {
    const ObString TMP_DEFAULT_HOST_NAME(OB_DEFAULT_HOST_NAME);
    for (int64_t i = 0; i < users_.count() && OB_SUCC(ret); ++i) {
      if (OB_FAIL(hosts_.push_back(TMP_DEFAULT_HOST_NAME))) {
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObLockUserArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              
              users_,
              locked_,
              hosts_);
  return len;
}



OB_SERIALIZE_MEMBER((ObAlterUserProfileArg, ObDDLArg),
                    
                    user_name_,
                    host_name_,
                    user_id_,
                    default_role_flag_,
                    role_id_array_,
                    user_ids_);

bool ObGrantArg::is_valid() const
{
  return true
         /* Oracle mode different permission system
          * && priv_level_ > OB_PRIV_INVALID_LEVEL
         && priv_level_ < OB_PRIV_MAX_LEVEL
         && users_passwd_.count() > 0
         && users_passwd_.count() == hosts_.count() * 2
         */;
}

bool ObGrantArg::is_allow_when_disable_ddl() const
{
  return is_inner_;
}


OB_DEF_SERIALIZE(ObGrantArg)
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_ENCODE,
              
              priv_level_,
              db_,
              table_,
              priv_set_,
              users_passwd_,
              need_create_user_,
              has_create_user_priv_,
              hosts_,
              roles_,
              option_,
              sys_priv_array_,
              obj_priv_array_,
              object_type_,
              object_id_,
              ins_col_ids_,
              upd_col_ids_,
              ref_col_ids_,
              grantor_id_,
              remain_roles_,
              is_inner_,
              sel_col_ids_,
              column_names_priv_,
              grantor_,
              grantor_host_,
              catalog_);
return ret;
}

OB_DEF_DESERIALIZE(ObGrantArg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((, ObDDLArg));
  LST_DO_CODE(OB_UNIS_DECODE,
              
              priv_level_,
              db_,
              table_,
              priv_set_,
              users_passwd_,
              need_create_user_,
              has_create_user_priv_,
              hosts_,
              roles_,
              option_,
              sys_priv_array_,
              obj_priv_array_,
              object_type_,
              object_id_,
              ins_col_ids_,
              upd_col_ids_,
              ref_col_ids_,
              grantor_id_,
              remain_roles_,
              is_inner_,
              sel_col_ids_,
              column_names_priv_,
              grantor_,
              grantor_host_,
              catalog_);

  //compatibility for old version
  if (OB_SUCC(ret) && users_passwd_.count() > 0 && hosts_.empty()) {
    const int64_t user_count = users_passwd_.count() / 2;
    const ObString TMP_DEFAULT_HOST_NAME(OB_DEFAULT_HOST_NAME);
    for (int64_t i = 0; i < user_count && OB_SUCC(ret); ++i) {
      if (OB_FAIL(hosts_.push_back(TMP_DEFAULT_HOST_NAME))) {
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObGrantArg)
{
  int64_t len = ObDDLArg::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              
              priv_level_,
              db_,
              table_,
              priv_set_,
              users_passwd_,
              need_create_user_,
              has_create_user_priv_,
              hosts_,
              roles_,
              option_,
              sys_priv_array_,
              obj_priv_array_,
              object_type_,
              object_id_,
              ins_col_ids_,
              upd_col_ids_,
              ref_col_ids_,
              grantor_id_,
              remain_roles_,
              is_inner_,
              sel_col_ids_,
              column_names_priv_,
              grantor_,
              grantor_host_,
              catalog_);
  return len;
}

bool ObRevokeUserArg::is_valid() const
{
  // FIXME: Currently the role only supports revoke from user
  return OB_INVALID_ID != user_id_;
}

OB_SERIALIZE_MEMBER((ObRevokeUserArg, ObDDLArg),
                    
                    user_id_,
                    priv_set_,
                    revoke_all_,
                    role_ids_);

bool ObRevokeCatalogArg::is_valid() const
{
  return OB_INVALID_ID != user_id_
      && !catalog_.empty();
}

OB_SERIALIZE_MEMBER((ObRevokeCatalogArg, ObDDLArg),
                    
                    user_id_,
                    catalog_,
                    priv_set_);

bool ObRevokeDBArg::is_valid() const
{
  return OB_INVALID_ID != user_id_
      && !db_.empty();
}

OB_SERIALIZE_MEMBER((ObRevokeDBArg, ObDDLArg),
                    
                    user_id_,
                    db_,
                    priv_set_);


bool ObRevokeTableArg::is_valid() const
{
  return OB_INVALID_ID != user_id_
      && !db_.empty() && !table_.empty();
}

OB_SERIALIZE_MEMBER((ObRevokeTableArg, ObDDLArg),
                    
                    user_id_,
                    db_,
                    table_,
                    priv_set_,
                    grant_,
                    obj_id_,
                    obj_type_,
                    grantor_id_,
                    obj_priv_array_,
                    revoke_all_ora_,
                    sel_col_ids_,
                    ins_col_ids_,
                    upd_col_ids_,
                    ref_col_ids_,
                    column_names_priv_,
                    grantor_,
                    grantor_host_);

bool ObRevokeRoutineArg::is_valid() const
{
  return OB_INVALID_ID != user_id_
      && !db_.empty() && !routine_.empty();
}


OB_SERIALIZE_MEMBER((ObRevokeRoutineArg, ObDDLArg),
                    
                    user_id_,
                    db_,
                    routine_,
                    priv_set_,
                    grant_,
                    obj_id_,
                    obj_type_,
                    grantor_id_,
                    obj_priv_array_,
                    revoke_all_ora_,
                    grantor_,
                    grantor_host_);

bool ObRevokeSysPrivArg::is_valid() const
{
  return OB_INVALID_ID != grantee_id_;
}


OB_SERIALIZE_MEMBER((ObRevokeSysPrivArg, ObDDLArg),
                    
                    grantee_id_,
                    sys_priv_array_,
                    role_ids_);

OB_SERIALIZE_MEMBER((ObCreateRoleArg, ObDDLArg),
                    
                    user_infos_);

//----End of structs for managing privileges----

OB_SERIALIZE_MEMBER(ObAdminMigrateReplicaArg, force_cmd_);







OB_SERIALIZE_MEMBER(ObServerZoneArg,
    server_, zone_);

OB_SERIALIZE_MEMBER(ObRefreshIOCalibrationArg,
                    storage_name_,
                    only_refresh_,
                    calibration_list_);



OB_SERIALIZE_MEMBER((ObAdminRefreshIOCalibrationArg, ObServerZoneArg),
                    storage_name_,
                    only_refresh_,
                    calibration_list_);

bool ObAdminRefreshIOCalibrationArg::is_valid() const
{
  bool bret = ObServerZoneArg::is_valid()
    && !(only_refresh_ && calibration_list_.count() > 0);
  return bret;
}



OB_SERIALIZE_MEMBER(ObAdminFlushCacheArg, batch_ids_, cache_type_, db_ids_, sql_id_, is_fine_grained_, ns_type_, schema_id_);


OB_SERIALIZE_MEMBER(ObFlushCacheArg, is_all_tenant_, cache_type_, db_ids_, sql_id_, is_fine_grained_, ns_type_, schema_id_);


bool ObAdminMergeArg::is_valid() const
{
  // empty zone means all zone
  return type_ >= START_MERGE && type_ <= RESUME_MERGE;
}


OB_SERIALIZE_MEMBER(ObAdminMergeArg,
   type_, affect_all_, affect_all_user_, affect_all_meta_);

bool ObAdminRecoveryArg::is_valid() const
{
  return type_ >= SUSPEND_RECOVERY && type_ <= RESUME_RECOVERY;
}

OB_SERIALIZE_MEMBER(ObAdminRecoveryArg, type_, zone_);

OB_SERIALIZE_MEMBER(ObAdminClearRoottableArg,
   tenant_name_);

OB_SERIALIZE_MEMBER(ObAdminSetConfigItem,
    name_, value_, comment_, zone_, server_, tenant_name_, batch_ids_,
    want_to_set_tenant_config_);

OB_SERIALIZE_MEMBER(ObAdminSetConfigArg, items_, is_inner_, is_backup_config_);



OB_SERIALIZE_MEMBER(ObAutoincSyncArg,
                    table_id_, column_id_, table_part_num_, auto_increment_, sync_value_);

OB_SERIALIZE_MEMBER(ObAdminChangeReplicaArg, force_cmd_);

bool ObUpdateIndexStatusArg::is_allow_when_disable_ddl() const
{
  bool bret = false;
  if (is_error_index_status(status_)) {
    bret = true;
  }
  return bret;
}

bool ObUpdateIndexStatusArg::is_valid() const
{
  return OB_INVALID_ID != index_table_id_ && status_ > INDEX_STATUS_NOT_FOUND
      && status_ < INDEX_STATUS_MAX;
}



bool ObUpdateMViewStatusArg::is_valid() const
{
  return (OB_INVALID_ID != mview_table_id_)
         && (ObMVAvailableFlag::IS_MV_UNAVAILABLE == mv_available_flag_
             || ObMVAvailableFlag::IS_MV_AVAILABLE == mv_available_flag_);
}

OB_SERIALIZE_MEMBER((ObUpdateIndexStatusArg, ObDDLArg),
                    index_table_id_,
                    status_,
                    convert_status_,
                    in_offline_ddl_white_list_,
                    data_table_id_,
                    database_name_,
                    task_id_,
                    error_code_);

OB_SERIALIZE_MEMBER((ObUpdateMViewStatusArg, ObDDLArg),
                    mview_table_id_,
                    mv_available_flag_,
                    convert_status_,
                    in_offline_ddl_white_list_);

OB_SERIALIZE_MEMBER(ObMergeFinishArg, server_, frozen_version_);

OB_SERIALIZE_MEMBER(ObDebugSyncActionArg, reset_, clear_, action_);







OB_SERIALIZE_MEMBER(ObMinorFreezeArg,
                    tablet_id_,
                    ls_id_);

int ObMinorFreezeArg::assign(const ObMinorFreezeArg &other)
{
  int ret = OB_SUCCESS;
  tablet_id_ = other.tablet_id_;
  ls_id_ = other.ls_id_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObRootMinorFreezeArg,
                    server_list_,
                    zone_,
                    tablet_id_,
                    ls_id_);


OB_SERIALIZE_MEMBER(ObTabletMajorFreezeArg,
                    
                    ls_id_,
                    tablet_id_,
                    is_rebuild_column_group_);


OB_SERIALIZE_MEMBER(ObCheckDanglingReplicaFinishArg, server_, version_, dangling_count_);




bool ObCreateOutlineArg::is_valid() const
{
  bool ret = !outline_info_.get_name_str().empty()
      && (!outline_info_.get_outline_content_str().empty() || outline_info_.has_outline_params());

  if (!outline_info_.is_format()) {
    ret = ret && !(outline_info_.get_sql_text_str().empty() &&
                !ObOutlineInfo::is_sql_id_valid(outline_info_.get_sql_id_str()))
              && !(outline_info_.get_signature_str().empty() &&
                !ObOutlineInfo::is_sql_id_valid(outline_info_.get_sql_id_str()));
  } else {
     ret = ret  && !(outline_info_.get_format_sql_text_str().empty() &&
                  !ObOutlineInfo::is_sql_id_valid(outline_info_.get_format_sql_id_str()))
                && !(outline_info_.get_signature_str().empty() &&
                  !ObOutlineInfo::is_sql_id_valid(outline_info_.get_format_sql_id_str()));
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObCreateOutlineArg, ObDDLArg),
                    or_replace_,
                    outline_info_,
                    db_name_);

OB_SERIALIZE_MEMBER((ObCreateUserDefinedFunctionArg, ObDDLArg),
                     udf_);

OB_SERIALIZE_MEMBER((ObDropUserDefinedFunctionArg, ObDDLArg),
                     
                     name_,
                     if_exist_);

OB_SERIALIZE_MEMBER((ObAlterOutlineArg, ObDDLArg),
                    alter_outline_info_,
                    db_name_);

bool ObDropOutlineArg::is_valid() const
{
  return !db_name_.empty() && !outline_name_.empty();
}

OB_SERIALIZE_MEMBER((ObDropOutlineArg, ObDDLArg),
                    
                    db_name_,
                    outline_name_,
                    is_format_);


OB_SERIALIZE_MEMBER((ObUseDatabaseArg, ObDDLArg));
OB_SERIALIZE_MEMBER(ObGetPartitionCountResult, partition_count_);

OB_SERIALIZE_MEMBER(ObAdminSetTPArg,
                    event_no_,
                    event_name_,
                    occur_,
                    trigger_freq_,
                    error_code_,
                    server_,
                    zone_,
                    cond_);

OB_SERIALIZE_MEMBER(ObRoutineDDLRes,
                    store_routine_schema_version_);

bool ObCreateRoutineArg::is_valid() const
{
  return !routine_info_.get_routine_name().empty()
      && routine_info_.get_routine_type() != INVALID_ROUTINE_TYPE
      && !routine_info_.get_routine_body().empty();
}


OB_SERIALIZE_MEMBER((ObCreateRoutineArg, ObDDLArg),
                    routine_info_, db_name_,
                    is_or_replace_, is_need_alter_,
                    error_info_, dependency_infos_, with_if_not_exist_);

bool ObDropRoutineArg::is_valid() const
{
  return !routine_name_.empty() && routine_type_ != INVALID_ROUTINE_TYPE;
}

OB_SERIALIZE_MEMBER((ObDropRoutineArg, ObDDLArg),
                    db_name_,
                    routine_name_, routine_type_,
                    if_exist_, error_info_);

bool ObCreatePackageArg::is_valid() const
{
  return !db_name_.empty() && package_info_.is_valid();
}


OB_SERIALIZE_MEMBER((ObCreatePackageArg, ObDDLArg), is_replace_,
                    is_editionable_, db_name_, package_info_,
                    public_routine_infos_, error_info_, dependency_infos_);

bool ObAlterPackageArg::is_valid() const
{
  return !db_name_.empty()
      && !package_name_.empty()
      && INVALID_PACKAGE_TYPE != package_type_;
}


OB_SERIALIZE_MEMBER((ObAlterPackageArg, ObDDLArg), db_name_, package_name_, package_type_,
                    compatible_mode_, public_routine_infos_, error_info_, exec_env_, dependency_infos_);

bool ObDropPackageArg::is_valid() const
{
  return !db_name_.empty()
      && !package_name_.empty();
}

OB_SERIALIZE_MEMBER((ObDropPackageArg, ObDDLArg),
                    db_name_, package_name_, package_type_,
                    compatible_mode_, error_info_);

bool ObCreateTriggerArg::is_valid() const
{
  return !trigger_database_.empty()
      && !base_object_name_.empty()
      && trigger_info_.is_valid_for_create();
}


OB_SERIALIZE_MEMBER((ObCreateTriggerArg, ObDDLArg),
                    trigger_database_,
                    base_object_database_,
                    base_object_name_,
                    trigger_info_,
                    flags_,
                    error_info_,
                    dependency_infos_);

OB_SERIALIZE_MEMBER(ObCreateTriggerRes,
                    table_schema_version_,
                    trigger_schema_version_);

bool ObDropTriggerArg::is_valid() const
{
  return true
      && !trigger_database_.empty()
      && !trigger_name_.empty();
}

OB_SERIALIZE_MEMBER((ObDropTriggerArg, ObDDLArg),
                    
                    trigger_database_,
                    trigger_name_,
                    if_exist_);

bool ObAlterTriggerArg::is_valid() const
{
  return trigger_infos_.count() != 0;
}


OB_SERIALIZE_MEMBER((ObAlterTriggerArg, ObDDLArg), trigger_database_,
                    trigger_info_, trigger_infos_, is_set_status_, is_alter_compile_);






OB_SERIALIZE_MEMBER(ObCancelTaskArg, task_id_);
OB_SERIALIZE_MEMBER(ObReportSingleReplicaArg, ls_id_);





DEF_TO_STRING(ObForceSetServerListArg)
{
  int64_t pos = 0;
  J_KV(K(server_list_), K(replica_num_));
  return pos;
}

OB_SERIALIZE_MEMBER(ObForceSetServerListArg, server_list_, replica_num_);

OB_SERIALIZE_MEMBER(ObForceSetServerListResult::LSFailedInfo, ls_id_, failed_ret_code_, failed_reason_);

OB_SERIALIZE_MEMBER(ObForceSetServerListResult::ResultInfo, successful_ls_, failed_ls_info_);

int ObForceSetServerListResult::ResultInfo::add_ls_info(const share::ObLSID ls_id, const int failed_ret)
{
  int ret = OB_SUCCESS;
  if (!ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ls_id), K(failed_ret));
  } else if (OB_SUCCESS == failed_ret) {
    if (OB_FAIL(successful_ls_.push_back(ls_id))) {
    }
  } else {
    const common::ObString failed_reason = ob_error_name(failed_ret);
    LSFailedInfo failed_info(ls_id, failed_ret, failed_reason);
    if (OB_FAIL(failed_ls_info_.push_back(failed_info))) {
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObForceSetServerListResult, ret_, result_list_);


DEF_TO_STRING(ObForceCreateSysTableArg)
{
  int64_t pos = 0;
  J_KV(
       K(table_id_),
       K(last_replay_log_id_));
  return pos;
}

OB_SERIALIZE_MEMBER(ObForceCreateSysTableArg, table_id_, last_replay_log_id_);

OB_SERIALIZE_MEMBER(ObSplitPartitionArg, split_info_);

DEF_TO_STRING(ObUpdateStatCacheArg)
{
  int64_t pos = 0;
  J_KV(K_(table_id),
       
       K_(partition_ids),
       K_(column_ids),
       K_(no_invalidate),
       K_(update_system_stats_only));
  return pos;
}
OB_SERIALIZE_MEMBER(ObUpdateStatCacheArg,
                    
                    table_id_,
                    partition_ids_,
                    column_ids_,
                    no_invalidate_,
                    update_system_stats_only_);
OB_SERIALIZE_MEMBER((ObSequenceDDLArg, ObDDLArg),
                    stmt_type_,
                    option_bitset_,
                    seq_schema_,
                    database_name_,
                    ignore_exists_error_);





OB_SERIALIZE_MEMBER(ObGetWRSArg, scope_, need_filter_);
OB_SERIALIZE_MEMBER(ObGetWRSResult, self_addr_, err_code_);

int64_t ObEstPartArgElement::get_serialize_size(void) const
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(scan_flag_);
  OB_UNIS_ADD_LEN(index_id_);
  OB_UNIS_ADD_LEN(range_columns_count_);
  OB_UNIS_ADD_LEN(batch_);
  OB_UNIS_ADD_LEN(tablet_id_);
  OB_UNIS_ADD_LEN(ls_id_);
  OB_UNIS_ADD_LEN(tx_id_);

  return len;
}

int ObEstPartArgElement::serialize(char *buf,
                                   const int64_t buf_len,
                                   int64_t &pos) const
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(scan_flag_);
  OB_UNIS_ENCODE(index_id_);
  OB_UNIS_ENCODE(range_columns_count_);
  OB_UNIS_ENCODE(batch_);
  OB_UNIS_ENCODE(tablet_id_);
  OB_UNIS_ENCODE(ls_id_);
  OB_UNIS_ENCODE(tx_id_);

  return ret;
}

int ObEstPartArgElement::deserialize(common::ObIAllocator &allocator,
                                     const char *buf,
                                     const int64_t data_len,
                                     int64_t &pos)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(scan_flag_);
  OB_UNIS_DECODE(index_id_);
  OB_UNIS_DECODE(range_columns_count_);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(batch_.deserialize(allocator, buf, data_len, pos))) {
    }
  }
  OB_UNIS_DECODE(tablet_id_);
  OB_UNIS_DECODE(ls_id_);
  OB_UNIS_DECODE(tx_id_);
  return ret;
}

void ObEstPartArg::reset()
{
  for (int64_t i = 0; i < index_params_.count(); ++i) {
    index_params_.at(i).batch_.destroy();
  }
}

OB_DEF_SERIALIZE_SIZE(ObEstPartArg)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(schema_version_);
  OB_UNIS_ADD_LEN(index_params_);
  return len;
}

OB_DEF_SERIALIZE(ObEstPartArg)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(schema_version_);
  OB_UNIS_ENCODE(index_params_);
  return ret;
}

OB_DEF_DESERIALIZE(ObEstPartArg)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(schema_version_);
  int64_t N = 0;
  OB_UNIS_DECODE(N);
  for (int64_t i = 0; OB_SUCC(ret) && i < N; i++) {
    ObEstPartArgElement arg;
    if (OB_FAIL(arg.deserialize(allocator_, buf, data_len, pos))) {
    } else if (OB_FAIL(index_params_.push_back(arg))) {
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObEstPartResElement, logical_row_count_,
                                         physical_row_count_,
                                         reliable_,
                                         est_records_);

OB_SERIALIZE_MEMBER(ObEstPartRes, index_param_res_);

int ObForceSetLSAsSingleReplicaArg::init(const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(false
                  || !ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ls_id));
  } else {
    
    ls_id_ = ls_id;
  }
  return ret;
}

bool ObForceSetLSAsSingleReplicaArg::is_valid() const
{
  return true && ls_id_.is_valid();
}

OB_SERIALIZE_MEMBER(ObForceSetLSAsSingleReplicaArg, ls_id_);



OB_SERIALIZE_MEMBER((ObDDLNopOpreatorArg, ObDDLArg),
                     schema_operation_);
OB_SERIALIZE_MEMBER(ObTenantSchemaVersions, tenant_schema_versions_);

int ObTenantSchemaVersions::add(const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  if (false) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_version));
  } else {
    TenantIdAndSchemaVersion info;
    
    info.schema_version_ = schema_version;
    if (OB_FAIL(tenant_schema_versions_.push_back(info))) {
    }
  }
  return ret;
}


OB_SERIALIZE_MEMBER((ObGetSchemaArg, ObDDLArg), reserve_, ignore_fail_);
OB_SERIALIZE_MEMBER(ObBroadcastSchemaArg, schema_version_, need_clear_ddl_epoch_);


OB_SERIALIZE_MEMBER(ObGetRecycleSchemaVersionsArg, batch_ids_);
bool ObGetRecycleSchemaVersionsArg::is_valid() const
{
  return batch_ids_.count() > 0;
}

OB_SERIALIZE_MEMBER(ObGetRecycleSchemaVersionsResult, recycle_schema_versions_);
void ObGetRecycleSchemaVersionsResult::reset()
{
  recycle_schema_versions_.reset();
}


OB_SERIALIZE_MEMBER(ObAdminAddDiskArg,
    diskgroup_name_, disk_path_, alias_name_, server_, zone_);
OB_SERIALIZE_MEMBER(ObAdminDropDiskArg,
    diskgroup_name_, alias_name_, server_, zone_);


int ObDDLRes::assign(const ObDDLRes &other)
{
  int ret = OB_SUCCESS;
  
  schema_id_ = other.schema_id_;
  task_id_ = other.task_id_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObDDLRes, schema_id_, task_id_);


OB_SERIALIZE_MEMBER(ObParallelDDLRes, schema_version_);
void ObAlterTableRes::reset()
{
  index_table_id_ = OB_INVALID_ID;
  constriant_id_ = OB_INVALID_ID;
  schema_version_ = OB_INVALID_VERSION;
  res_arg_array_.reset();
  ddl_type_ = ObDDLType::DDL_INVALID;
  task_id_ = 0;
  ddl_res_array_.reset();
  ddl_need_retry_at_executor_ = false;
}



OB_SERIALIZE_MEMBER(ObDropDatabaseRes, ddl_res_, affected_row_);
OB_SERIALIZE_MEMBER(ObAlterTableResArg, schema_type_, schema_id_, schema_version_, part_object_id_);
OB_SERIALIZE_MEMBER(ObAlterTableRes, index_table_id_, constriant_id_, schema_version_,
res_arg_array_, ddl_type_, task_id_, ddl_res_array_, ddl_need_retry_at_executor_);
OB_SERIALIZE_MEMBER(ObGetTenantSchemaVersionArg);
OB_SERIALIZE_MEMBER(ObGetTenantSchemaVersionResult, schema_version_);

OB_SERIALIZE_MEMBER(ObCheckServerEmptyArg, mode_, sys_data_version_, server_id_);
int ObCheckServerEmptyArg::init(const Mode &mode, const uint64_t &sys_data_version, const uint64_t &server_id)
{
  int ret = OB_SUCCESS;
  mode_ = mode;
  sys_data_version_ = sys_data_version;
  server_id_ = server_id;
  if (mode == ADD_SERVER) {
    if (server_id != OB_INVALID_ID) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("server id is not invalid in add server mode", KR(ret), K(mode), K(server_id));
    }
  } else if (mode == BOOTSTRAP) {
    if (server_id == OB_INVALID_ID) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("server id is invalid in bootstrap mode", KR(ret), K(mode), K(server_id));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unknown mode", KR(ret), K(mode));
  }
  return ret;
}
OB_SERIALIZE_MEMBER(ObCheckServerEmptyResult, server_empty_, zone_);
int ObCheckServerEmptyResult::init(const bool &server_empty, const ObZone &zone)
{
  int ret = OB_SUCCESS;
  server_empty_ = server_empty;
  if (OB_FAIL(zone_.assign(zone))) {
  }
  return ret;
}












































OB_SERIALIZE_MEMBER(CheckLeaderRpcIndex, switchover_timestamp_, epoch_,
                    ml_pk_index_, pkey_info_start_index_);


bool CheckLeaderRpcIndex::is_valid() const
{
  return switchover_timestamp_ > 0 && epoch_ >= 0
      && ml_pk_index_ >= 0 && pkey_info_start_index_ >= 0;
}

void CheckLeaderRpcIndex::reset()
{
  switchover_timestamp_ = 0;
  epoch_ = -1;
  ml_pk_index_ = -1;
  pkey_info_start_index_ = -1;
  
}







OB_SERIALIZE_MEMBER(ObRefreshTimezoneArg);


OB_DEF_SERIALIZE(ObDDLBuildSingleReplicaRequestArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, ls_id_, source_tablet_id_, dest_tablet_id_,
    source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
    parallelism_, tablet_task_id_, data_format_version_, consumer_group_id_,
    dest_ls_id_, dest_schema_version_,
    compaction_scn_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_, parallel_datum_rowkey_list_, is_no_logging_,
    min_split_start_scn_);
  return ret;
}

OB_DEF_DESERIALIZE(ObDDLBuildSingleReplicaRequestArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, ls_id_, source_tablet_id_, dest_tablet_id_,
      source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
      parallelism_, tablet_task_id_, data_format_version_, consumer_group_id_,
      dest_ls_id_, dest_schema_version_,
      compaction_scn_, can_reuse_macro_block_, split_sstable_type_,
      lob_col_idxs_);
  if (FAILEDx(ObSplitUtil::deserializ_parallel_datum_rowkey(
        rowkey_allocator_, buf, data_len, pos, parallel_datum_rowkey_list_))) {
  }

  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_DECODE, is_no_logging_, min_split_start_scn_);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDDLBuildSingleReplicaRequestArg)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, ls_id_, source_tablet_id_, dest_tablet_id_,
    source_table_id_, dest_schema_id_, schema_version_, snapshot_version_, ddl_type_, task_id_, execution_id_,
    parallelism_, tablet_task_id_, data_format_version_, consumer_group_id_,
    dest_ls_id_, dest_schema_version_,
    compaction_scn_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_, parallel_datum_rowkey_list_, is_no_logging_,
    min_split_start_scn_);
  return len;
}

bool ObDDLBuildSingleReplicaRequestArg::is_valid() const
{
  bool is_valid = ls_id_.is_valid() && source_tablet_id_.is_valid() && dest_tablet_id_.is_valid()
               && OB_INVALID_ID != source_table_id_ && OB_INVALID_ID != dest_schema_id_
               && schema_version_ > 0 && snapshot_version_ > 0 && task_id_ > 0 && parallelism_ > 0
               && tablet_task_id_ > 0 && data_format_version_ > 0 && consumer_group_id_ >= 0
               && dest_ls_id_.is_valid() && dest_schema_version_ > 0;
  return is_valid;
}

int ObDDLBuildSingleReplicaRequestArg::assign(const ObDDLBuildSingleReplicaRequestArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(other));
  } else if (OB_FAIL(lob_col_idxs_.assign(other.lob_col_idxs_))) {
  } else if (OB_FAIL(parallel_datum_rowkey_list_.assign(other.parallel_datum_rowkey_list_))) {
  } else {
    
    ls_id_ = other.ls_id_;
    
    dest_ls_id_ = other.dest_ls_id_;
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
    consumer_group_id_ = other.consumer_group_id_;
    compaction_scn_ = other.compaction_scn_;
    can_reuse_macro_block_ = other.can_reuse_macro_block_;
    split_sstable_type_ = other.split_sstable_type_;
    min_split_start_scn_ = other.min_split_start_scn_;
    is_no_logging_ = other.is_no_logging_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObDDLBuildSingleReplicaRequestResult, ret_code_, row_inserted_, row_scanned_, physical_row_count_);


OB_SERIALIZE_MEMBER(ObDDLBuildSingleReplicaResponseArg, ls_id_, tablet_id_,
                    source_table_id_, dest_schema_id_, ret_code_, snapshot_version_, schema_version_,
                    task_id_, execution_id_, row_scanned_, row_inserted_, dest_ls_id_, dest_schema_version_,
                    server_addr_, physical_row_count_);


// === Functions for tablet split start. ===
OB_SERIALIZE_MEMBER(ObPrepareSplitRangesArg, ls_id_, tablet_id_,
    user_parallelism_, schema_tablet_size_, ddl_type_);
OB_DEF_SERIALIZE(ObPrepareSplitRangesRes)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, parallel_datum_rowkey_list_);
  return ret;
}

OB_DEF_DESERIALIZE(ObPrepareSplitRangesRes)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObSplitUtil::deserializ_parallel_datum_rowkey(
      rowkey_allocator_, buf, data_len, pos, parallel_datum_rowkey_list_))) {
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObPrepareSplitRangesRes)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, parallel_datum_rowkey_list_);
  return len;
}

bool ObTabletSplitArg::is_valid() const
{
  bool is_valid = ls_id_.is_valid() && OB_INVALID_ID != table_id_
      && schema_version_ > 0 && task_id_ > 0
      && source_tablet_id_.is_valid() && dest_tablets_id_.count() > 0
      && compaction_scn_ > 0
      && data_format_version_ > 0 && consumer_group_id_ >= 0
      && split_sstable_type_ >= share::ObSplitSSTableType::SPLIT_BOTH
      && split_sstable_type_ <= share::ObSplitSSTableType::SPLIT_MINOR;
  if (!lob_col_idxs_.empty()) {
    is_valid = is_valid && (OB_INVALID_ID != lob_table_id_);
  }
  return is_valid;
}

int ObTabletSplitArg::assign(const ObTabletSplitArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(other));
  } else if (OB_FAIL(dest_tablets_id_.assign(other.dest_tablets_id_))) {
  } else if (OB_FAIL(lob_col_idxs_.assign(other.lob_col_idxs_))) {
  } else if (OB_FAIL(parallel_datum_rowkey_list_.assign(other.parallel_datum_rowkey_list_))) {
  } else {
    ls_id_                 = other.ls_id_;
    table_id_              = other.table_id_;
    lob_table_id_          = other.lob_table_id_;
    schema_version_        = other.schema_version_;
    task_id_               = other.task_id_;
    source_tablet_id_      = other.source_tablet_id_;
    compaction_scn_        = other.compaction_scn_;
    data_format_version_   = other.data_format_version_;
    consumer_group_id_     = other.consumer_group_id_;
    can_reuse_macro_block_ = other.can_reuse_macro_block_;
    split_sstable_type_    = other.split_sstable_type_;
    min_split_start_scn_   = other.min_split_start_scn_;
  }
  return ret;
}

bool ObTabletSplitStartArg::is_valid() const
{
  bool is_valid = true;
  for (int64_t i = 0; is_valid && i < split_info_array_.count(); i++) {
    is_valid = is_valid && split_info_array_.at(i).is_valid();
  }
  return is_valid;
}



bool ObTabletSplitFinishArg::is_valid() const
{
  bool is_valid = true;
  for (int64_t i = 0; is_valid && i < split_info_array_.count(); i++) {
    is_valid = is_valid && split_info_array_.at(i).is_valid();
  }
  return is_valid;
}



OB_DEF_SERIALIZE(ObTabletSplitArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, ls_id_, table_id_, lob_table_id_,
    schema_version_, task_id_, source_tablet_id_,
    dest_tablets_id_, compaction_scn_, data_format_version_,
    consumer_group_id_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_, parallel_datum_rowkey_list_, min_split_start_scn_);
  return ret;
}

OB_DEF_DESERIALIZE(ObTabletSplitArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, ls_id_, table_id_, lob_table_id_,
    schema_version_, task_id_, source_tablet_id_,
    dest_tablets_id_, compaction_scn_, data_format_version_,
    consumer_group_id_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_);
  if (FAILEDx(ObSplitUtil::deserializ_parallel_datum_rowkey(
      rowkey_allocator_, buf, data_len, pos, parallel_datum_rowkey_list_))) {
  } else {
    LST_DO_CODE(OB_UNIS_DECODE, min_split_start_scn_);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObTabletSplitArg)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, ls_id_, table_id_, lob_table_id_,
    schema_version_, task_id_, source_tablet_id_,
    dest_tablets_id_, compaction_scn_, data_format_version_,
    consumer_group_id_, can_reuse_macro_block_, split_sstable_type_,
    lob_col_idxs_, parallel_datum_rowkey_list_, min_split_start_scn_);
  return len;
}

OB_SERIALIZE_MEMBER(ObTabletSplitStartArg, split_info_array_);
OB_SERIALIZE_MEMBER(ObTabletSplitStartResult, ret_codes_, min_split_start_scn_);
OB_SERIALIZE_MEMBER(ObTabletSplitFinishArg, split_info_array_);
OB_SERIALIZE_MEMBER(ObTabletSplitFinishResult, ret_codes_);


OB_SERIALIZE_MEMBER(ObFreezeSplitSrcTabletArg, ls_id_, tablet_ids_);


OB_SERIALIZE_MEMBER(ObFreezeSplitSrcTabletRes, data_end_scn_);

int ObAutoSplitTabletArg::assign(const ObAutoSplitTabletArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(other));
  } else {
    ls_id_ = other.ls_id_;
    tablet_id_ = other.tablet_id_;
    
    auto_split_tablet_size_ = other.auto_split_tablet_size_;
    used_disk_space_ = other.used_disk_space_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObAutoSplitTabletArg, ls_id_, tablet_id_,
    auto_split_tablet_size_, used_disk_space_);


bool ObAutoSplitTabletBatchArg::is_valid() const
{
  bool valid = (args_.count() > 0);
  for (int64_t i = 0; valid && i < args_.count(); ++i)
  {
    valid = args_.at(i).is_valid();
  }
  return valid;
}

OB_SERIALIZE_MEMBER(ObAutoSplitTabletBatchArg, args_);

bool ObAutoSplitTabletBatchRes::is_valid() const
{
  return (rets_.count() > 0) && (suggested_next_valid_time_ != OB_INVALID_TIMESTAMP);
}

OB_SERIALIZE_MEMBER(ObAutoSplitTabletBatchRes, rets_, suggested_next_valid_time_);


OB_SERIALIZE_MEMBER(ObFetchSplitTabletInfoArg, ls_id_, tablet_ids_);


OB_SERIALIZE_MEMBER(ObFetchSplitTabletInfoRes, tablet_sizes_, create_commit_versions_);

// === Functions for tablet split end. ===


OB_SERIALIZE_MEMBER((ObCreateDirectoryArg, ObDDLArg), or_replace_, user_id_, schema_);


OB_SERIALIZE_MEMBER((ObDropDirectoryArg, ObDDLArg), directory_name_);

bool ObBatchRemoveTabletArg::is_valid() const
{
  bool is_valid = id_.is_valid();
  for (int64_t i = 0; i < tablet_ids_.count() && is_valid; i++) {
    is_valid = tablet_ids_.at(i).is_valid();
  }
  return is_valid;
}

void ObBatchRemoveTabletArg::reset()
{
  tablet_ids_.reset();
  id_.reset();
  is_old_mds_ = false;
}

int ObBatchRemoveTabletArg::assign(const ObBatchRemoveTabletArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tablet_ids_.assign(arg.tablet_ids_))) {
  } else {
    id_ = arg.id_;
    is_old_mds_ = arg.is_old_mds_;
  }
  return ret;
}

int ObBatchRemoveTabletArg::init(const ObIArray<common::ObTabletID> &tablet_ids,
                          const share::ObLSID id)
{
  int ret = OB_SUCCESS;
  bool is_valid = id.is_valid();
  for (int64_t i = 0; i < tablet_ids.count() && is_valid; i++) {
    is_valid = tablet_ids.at(i).is_valid();
  }
  if (OB_UNLIKELY(!is_valid)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_ids), K(id));
  } else if (OB_FAIL(tablet_ids_.assign(tablet_ids))) {
  } else {
    id_ = id;
  }
  return ret;
}

int ObBatchRemoveTabletArg::skip_array_len(const char *buf,
    int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (pos > data_len) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid args", K(ret), KP(buf), K(data_len), K(pos));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
  } else if (count <= 0) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid args", K(ret), KP(buf), K(data_len), K(pos), K(count));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      ObTabletID tablet_id;
      OB_UNIS_DECODE(tablet_id);
    }
  }
  return ret;
}

int ObBatchRemoveTabletArg::is_old_mds(const char *buf,
    int64_t data_len,
    bool &is_old_mds)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  is_old_mds = false;
  int64_t version = 0;
  int64_t len = 0;
  share::ObLSID id;

  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid args", K(ret), KP(buf), K(data_len));
  } else {
    LST_DO_CODE(OB_UNIS_DECODE, version, len);
    if (OB_FAIL(ret)) {
    }
    // tablets array
    else if (OB_FAIL(skip_array_len(buf, data_len, pos))) {
    } else {
      LST_DO_CODE(OB_UNIS_DECODE, id, is_old_mds);
    }
  }

  return ret;
}

DEF_TO_STRING(ObBatchRemoveTabletArg)
{
  int64_t pos = 0;
  J_KV(K_(id), K_(tablet_ids), K_(is_old_mds));
  return pos;
}

OB_DEF_SERIALIZE(ObBatchRemoveTabletArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, tablet_ids_, id_, is_old_mds_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObBatchRemoveTabletArg)
{
  int len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, tablet_ids_, id_, is_old_mds_);
  return len;
}


OB_SERIALIZE_MEMBER((ObPartitionSplitArg, ObDDLArg),
                    src_tablet_id_,
                    dest_tablet_ids_,
                    local_index_table_ids_,
                    local_index_schema_versions_,
                    src_local_index_tablet_ids_,
                    dest_local_index_tablet_ids_,
                    lob_table_ids_,
                    lob_schema_versions_,
                    src_lob_tablet_ids_,
                    dest_lob_tablet_ids_,
                    task_type_,
                    src_ls_id_);

OB_SERIALIZE_MEMBER(ObCleanSplittedTabletArg,
                    
                    table_id_,
                    task_id_,
                    local_index_table_ids_,
                    lob_table_ids_,
                    src_table_tablet_id_,
                    dest_tablet_ids_,
                    src_local_index_tablet_ids_,
                    dest_local_index_tablet_ids_,
                    src_lob_tablet_ids_,
                    dest_lob_tablet_ids_,
                    is_auto_split_);

int ObCheckMemtableCntArg::assign(const ObCheckMemtableCntArg &other)
{
  int ret = OB_SUCCESS;
  
  ls_id_ = other.ls_id_;
  tablet_id_ = other.tablet_id_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObCheckMemtableCntArg,
                    
                    ls_id_,
                    tablet_id_);


OB_SERIALIZE_MEMBER(ObCheckMemtableCntResult,
                    memtable_cnt_);

int ObCheckMediumCompactionInfoListArg::assign(const ObCheckMediumCompactionInfoListArg &other)
{
  int ret = OB_SUCCESS;
  
  ls_id_ = other.ls_id_;
  tablet_id_ = other.tablet_id_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObCheckMediumCompactionInfoListArg,
                    
                    ls_id_,
                    tablet_id_);

OB_SERIALIZE_MEMBER(ObCheckMediumCompactionInfoListResult,
                    info_list_cnt_,
                    primary_compaction_scn_);

OB_DEF_DESERIALIZE(ObBatchRemoveTabletArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, tablet_ids_, id_);
  if (OB_SUCC(ret)) {
    if (pos == data_len) {
      is_old_mds_ = true;
    } else {
      LST_DO_CODE(OB_UNIS_DECODE, is_old_mds_);
    }
  }
  return ret;
}

// ----------------------
bool ObCreateTabletInfo::is_valid() const
{
  bool is_valid = data_tablet_id_.is_valid()
                  && table_schema_index_.count() > 0
                  && table_schema_index_.count() == tablet_ids_.count()
                  && lib::Worker::CompatMode::INVALID != compat_mode_
                  && (create_commit_versions_.empty() || create_commit_versions_.count() == tablet_ids_.count());
  for (int64_t i = 0; i < tablet_ids_.count() && is_valid; i++) {
    is_valid = tablet_ids_.at(i).is_valid();
  }
  return is_valid;
}

void ObCreateTabletInfo::reset()
{
  tablet_ids_.reset();
  data_tablet_id_.reset();
  table_schema_index_.reset();
  compat_mode_ = lib::Worker::CompatMode::INVALID;
  is_create_bind_hidden_tablets_ = false;
  create_commit_versions_.reset();
  has_cs_replica_ = false;
  fork_tablet_infos_.reset();
}

int ObCreateTabletInfo::assign(const ObCreateTabletInfo &info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("info is invalid", KR(ret), K(info));
  } else if (OB_FAIL(tablet_ids_.assign(info.tablet_ids_))) {
  } else if (OB_FAIL(table_schema_index_.assign(info.table_schema_index_))) {
  } else if (OB_FAIL(create_commit_versions_.assign(info.create_commit_versions_))) {
  } else if (OB_FAIL(fork_tablet_infos_.assign(info.fork_tablet_infos_))) {
  } else {
    data_tablet_id_ = info.data_tablet_id_;
    compat_mode_ = info.compat_mode_;
    is_create_bind_hidden_tablets_ = info.is_create_bind_hidden_tablets_;
    has_cs_replica_ = info.has_cs_replica_;
  }
  return ret;
}

int ObCreateTabletInfo::init(const ObIArray<common::ObTabletID> &tablet_ids,
                             common::ObTabletID data_tablet_id,
                             const common::ObIArray<int64_t> &table_schema_index,
                             const lib::Worker::CompatMode &mode,
                             const bool is_create_bind_hidden_tablets,
                             const ObIArray<int64_t> &create_commit_versions,
                             const bool has_cs_replica)
{
  int ret = OB_SUCCESS;
  bool is_valid = data_tablet_id.is_valid()
                  // && OB_INVALID_VERSION != schema_version
                  && table_schema_index.count() > 0
                  && table_schema_index.count() == tablet_ids.count();
  for (int64_t i = 0; i < tablet_ids.count() && is_valid; i++) {
    is_valid = tablet_ids.at(i).is_valid();
  }
  if (OB_UNLIKELY(!is_valid)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_ids), K(data_tablet_id), K(table_schema_index));
  } else if (OB_FAIL(tablet_ids_.assign(tablet_ids))) {
  } else if (OB_FAIL(table_schema_index_.assign(table_schema_index))) {
  } else if (OB_FAIL(create_commit_versions_.assign(create_commit_versions))) {
  } else {
    data_tablet_id_ = data_tablet_id;
    compat_mode_ = mode;
    is_create_bind_hidden_tablets_ = is_create_bind_hidden_tablets;
    has_cs_replica_ = has_cs_replica;
  }
  return ret;
}

int ObCreateTabletInfo::init(const ObIArray<common::ObTabletID> &tablet_ids,
                             common::ObTabletID data_tablet_id,
                             const common::ObIArray<int64_t> &table_schema_index,
                             const lib::Worker::CompatMode &mode,
                             const bool is_create_bind_hidden_tablets,
                             const ObIArray<int64_t> &create_commit_versions,
                             const bool has_cs_replica,
                             const ObIArray<share::ObForkTabletInfo> &fork_tablet_infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(fork_tablet_infos.count() != 0 && fork_tablet_infos.count() != tablet_ids.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fork tablet infos count not match tablet ids count", KR(ret), K(tablet_ids.count()),
      K(fork_tablet_infos));
  } else if (OB_FAIL(init(tablet_ids, data_tablet_id, table_schema_index, mode, is_create_bind_hidden_tablets,
      create_commit_versions, has_cs_replica))) {
  } else if (OB_FAIL(fork_tablet_infos_.assign(fork_tablet_infos))) {
  }
  return ret;
}

int ObCreateTabletInfo::get_fork_tablet_info(const int64_t idx, share::ObForkTabletInfo &fork_tablet_info) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(idx < 0 || idx >= tablet_ids_.count() || (fork_tablet_infos_.count() > 0 && idx >= fork_tablet_infos_.count()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid index", KR(ret), K(idx), "tablet_count", tablet_ids_.count(), "fork_tablet_infos_count", fork_tablet_infos_.count());
  } else if (fork_tablet_infos_.empty()) {
    fork_tablet_info.reset();
  } else {
    fork_tablet_info = fork_tablet_infos_.at(idx);
  }
  return ret;
}

DEF_TO_STRING(ObCreateTabletInfo)
{
  int64_t pos = 0;
  J_KV(K_(tablet_ids), K_(data_tablet_id), K_(table_schema_index), K_(compat_mode), K_(is_create_bind_hidden_tablets), K_(create_commit_versions), K_(has_cs_replica), K_(fork_tablet_infos));
  return pos;
}

OB_SERIALIZE_MEMBER(ObCreateTabletInfo, tablet_ids_, data_tablet_id_, table_schema_index_, compat_mode_, is_create_bind_hidden_tablets_, create_commit_versions_, has_cs_replica_, fork_tablet_infos_);

int ObCreateTabletExtraInfo::init(const uint64_t tenant_data_version,
                                  const bool need_create_empty_major,
                                  const bool micro_index_clustered,
                                  const ObTabletID &split_src_tablet_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(tenant_data_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg",
             K(ret), K(tenant_data_version), K(need_create_empty_major),
             K(micro_index_clustered), K(split_src_tablet_id));
  } else {
    tenant_data_version_ = tenant_data_version;
    need_create_empty_major_ = need_create_empty_major;
    micro_index_clustered_ = micro_index_clustered;
    split_src_tablet_id_ = split_src_tablet_id;
  }
  return ret;
}

void ObCreateTabletExtraInfo::reset()
{
  need_create_empty_major_ = true;
  tenant_data_version_ = 0;
  micro_index_clustered_ = false;
  split_src_tablet_id_.reset();
  split_can_reuse_macro_block_ = false;
}

int ObCreateTabletExtraInfo::assign(const ObCreateTabletExtraInfo &other)
{
  int ret = OB_SUCCESS;
  tenant_data_version_ = other.tenant_data_version_;
  need_create_empty_major_ = other.need_create_empty_major_;
  micro_index_clustered_ = other.micro_index_clustered_;
  split_src_tablet_id_ = other.split_src_tablet_id_;
  split_can_reuse_macro_block_ = other.split_can_reuse_macro_block_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObCreateTabletExtraInfo,
                    tenant_data_version_,
                    need_create_empty_major_,
                    micro_index_clustered_,
                    split_src_tablet_id_,
                    split_can_reuse_macro_block_);

bool ObBatchCreateTabletArg::is_inited() const
{
  return id_.is_valid() && major_frozen_scn_.is_valid();
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
  id_.reset();
  major_frozen_scn_.reset();
  tablets_.reset();
  table_schemas_.reset();
  need_check_tablet_cnt_ = false;
  is_old_mds_ = false;
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
  } else if (OB_FAIL(table_schemas_.assign(arg.table_schemas_))) {
  } else if (OB_FAIL(tablet_extra_infos_.assign(arg.tablet_extra_infos_))) {
  } else if (OB_FAIL(create_tablet_schemas_.reserve(create_tablet_schemas.count()))) {
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
    id_ = arg.id_;
    major_frozen_scn_ = arg.major_frozen_scn_;
    need_check_tablet_cnt_ = arg.need_check_tablet_cnt_;
    is_old_mds_ = arg.is_old_mds_;
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
    min_data_version = std::min(min_data_version, tablet_extra_infos_.at(i).tenant_data_version_);
  }
  if (UINT64_MAX != min_data_version) {
    bool_ret = true;
  }
  return bool_ret;
}

OB_SERIALIZE_MEMBER((ObContextDDLArg, ObDDLArg),
                    stmt_type_,
                    ctx_schema_,
                    or_replace_);


int ObBatchCreateTabletArg::init_create_tablet(
  const share::ObLSID &id,
  const SCN &major_frozen_scn,
  const bool need_check_tablet_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!id.is_valid() || !major_frozen_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(id), K(major_frozen_scn));
  } else {
    id_ = id;
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
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < create_tablet_schemas_.count(); ++i) {
    if (OB_ISNULL(create_tablet_schemas_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", KR(ret), K(i), KPC(this));
    } else if (OB_FAIL(create_tablet_schemas_.at(i)->serialize(buf, data_len, pos))) {
    }
  }
  return ret;
}

int ObBatchCreateTabletArg::skip_unis_array_len(const char *buf,
    int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (pos > data_len) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid args", K(ret), KP(buf), K(data_len), K(pos));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
  } else if (count < 0) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid args", K(ret), KP(buf), K(data_len), K(pos), K(count));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
      int64_t version = 0;
      int64_t len = 0;
      OB_UNIS_DECODE(version);
      OB_UNIS_DECODE(len);
      CHECK_VERSION_LENGTH(1, version, len);
      pos += len;
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
  } else if (count < 0) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "count invalid", KR(ret), K(buf), K(data_len), K(pos), K(count));
  } else if (count == 0) {
    STORAGE_LOG(INFO, "upgrade, count is 0", KR(ret), K(buf), K(data_len), K(pos), K(count));
  } else if (OB_FAIL(create_tablet_schemas_.reserve(count))) {
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

int ObBatchCreateTabletArg::is_old_mds(const char *buf,
    int64_t data_len,
    bool &is_old_mds)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  is_old_mds = false;
  int64_t version = 0;
  int64_t len = 0;
  share::ObLSID id;
  share::SCN major_frozen_scn;
  bool need_check_tablet_cnt = false;

  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid args", K(ret), KP(buf), K(data_len));
  } else {
    LST_DO_CODE(OB_UNIS_DECODE, version, len, id, major_frozen_scn);
    if (OB_FAIL(ret)) {
    }
    // tablets array
    else if (OB_FAIL(skip_unis_array_len(buf, data_len, pos))) {
    }
    // schema array
    else if (OB_FAIL(skip_unis_array_len(buf, data_len, pos))) {
    } else {
      LST_DO_CODE(OB_UNIS_DECODE, need_check_tablet_cnt, is_old_mds);
    }
  }

  return ret;
}

DEF_TO_STRING(ObBatchCreateTabletArg)
{
  int64_t pos = 0;
  J_KV(K_(id), K_(major_frozen_scn), K_(need_check_tablet_cnt), K_(is_old_mds), K_(tablets), K_(tablet_extra_infos), K_(clog_checkpoint_scn), K_(create_type), K_(mds_checkpoint_scn));
  return pos;
}

OB_DEF_SERIALIZE(ObBatchCreateTabletArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, id_, major_frozen_scn_, tablets_, table_schemas_, need_check_tablet_cnt_, is_old_mds_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(serialize_for_create_tablet_schemas(buf, buf_len, pos))) {
  } else {
    OB_UNIS_ENCODE_ARRAY(tablet_extra_infos_, tablet_extra_infos_.count());
  }
  LST_DO_CODE(OB_UNIS_ENCODE, clog_checkpoint_scn_, create_type_, mds_checkpoint_scn_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObBatchCreateTabletArg)
{
  int len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, id_, major_frozen_scn_, tablets_, table_schemas_, need_check_tablet_cnt_, is_old_mds_);
  len += get_serialize_size_for_create_tablet_schemas();
  OB_UNIS_ADD_LEN_ARRAY(tablet_extra_infos_, tablet_extra_infos_.count());
  LST_DO_CODE(OB_UNIS_ADD_LEN, clog_checkpoint_scn_, create_type_, mds_checkpoint_scn_);
  return len;
}

OB_DEF_DESERIALIZE(ObBatchCreateTabletArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, id_, major_frozen_scn_, tablets_, table_schemas_, need_check_tablet_cnt_);
  if (OB_SUCC(ret)) {
    if (pos == data_len) {
      is_old_mds_ = true;
    } else {
      LST_DO_CODE(OB_UNIS_DECODE, is_old_mds_);
      if (OB_FAIL(ret)) {
      } else if (pos == data_len) {
      } else if (OB_FAIL(deserialize_create_tablet_schemas(buf, data_len, pos))) {
      } else {
        int64_t tablet_extra_infos_count = 0;
        OB_UNIS_DECODE(tablet_extra_infos_count);
        if (tablet_extra_infos_count > 0 && OB_FAIL(tablet_extra_infos_.prepare_allocate(tablet_extra_infos_count))) {
          LOG_WARN("prepare allocate failed", K(ret), K(tablet_extra_infos_count));
        } else {
          OB_UNIS_DECODE_ARRAY(tablet_extra_infos_, tablet_extra_infos_count);
        }
      }
    }
  }

  if (OB_SUCC(ret) && tablet_extra_infos_.empty()) {
    // process the compatibility of the ObCreateTabletExtraInfo.
    const int64_t schemas_count = create_tablet_schemas_.empty() ? table_schemas_.count() : create_tablet_schemas_.count();
    if (OB_UNLIKELY(schemas_count <= 0)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid arg", K(ret), K(schemas_count), K(table_schemas_), K(create_tablet_schemas_));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < schemas_count; i++) {
        obcall::ObCreateTabletExtraInfo create_tablet_extra_info; // placeholder.
        if (OB_FAIL(tablet_extra_infos_.push_back(create_tablet_extra_info))) {
        }
      }
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE, clog_checkpoint_scn_, create_type_, mds_checkpoint_scn_);
  return ret;
}

bool ObFetchTabletSeqArg::is_valid() const
{
  return true
         && tablet_id_.is_valid();
}




DEF_TO_STRING(ObFetchTabletSeqArg)
{
  int64_t pos = 0;
  J_KV(K_(tablet_id), K_(ls_id));
  return pos;
}

OB_SERIALIZE_MEMBER(ObFetchTabletSeqArg, cache_size_, tablet_id_, ls_id_);

bool ObFetchTabletSeqRes::is_valid() const
{
  return true
         && cache_interval_.is_valid();
}

void ObFetchTabletSeqRes::reset()
{
  cache_interval_.reset();
  
}



DEF_TO_STRING(ObFetchTabletSeqRes)
{
  int64_t pos = 0;
  J_KV(K_(cache_interval));
  return pos;
}

OB_SERIALIZE_MEMBER(ObFetchTabletSeqRes, cache_interval_);

ObCallRemoteWriteDDLRedoLogArg::ObCallRemoteWriteDDLRedoLogArg()
  : ls_id_(), redo_info_(), task_id_(0)
{}

int ObCallRemoteWriteDDLRedoLogArg::init(const share::ObLSID &ls_id,
                                        const storage::ObDDLMacroBlockRedoInfo &redo_info,
                                        const int64_t task_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(false || task_id == 0 || !ls_id.is_valid() || !redo_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("args are not valid", K(ret), K(task_id), K(ls_id), K(redo_info));
  } else {
    
    ls_id_ = ls_id;
    redo_info_ = redo_info;
    task_id_ = task_id;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObCallRemoteWriteDDLRedoLogArg, ls_id_, redo_info_, task_id_);

ObCallRemoteWriteDDLCommitLogArg::ObCallRemoteWriteDDLCommitLogArg()
  : ls_id_(), table_key_(), start_scn_(SCN::min_scn()),
    table_id_(0), execution_id_(-1), ddl_task_id_(0)
{}

int ObCallRemoteWriteDDLCommitLogArg::init(const share::ObLSID &ls_id,
                                          const storage::ObITable::TableKey &table_key,
                                          const SCN &start_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(false || !ls_id.is_valid() || !table_key.is_valid() || !start_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet id is not valid", K(ret), K(ls_id), K(table_key), K(start_scn));
  } else {
    
    ls_id_ = ls_id;
    table_key_ = table_key;
    start_scn_ = start_scn;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObCallRemoteWriteDDLCommitLogArg, ls_id_, table_key_, start_scn_,
                    table_id_, execution_id_, ddl_task_id_);


ObCallRemoteWriteDDLIncCommitLogArg::ObCallRemoteWriteDDLIncCommitLogArg()
  : ls_id_(), tablet_id_(), lob_meta_tablet_id_(), tx_desc_(nullptr), need_release_(false)
{}

ObCallRemoteWriteDDLIncCommitLogArg::~ObCallRemoteWriteDDLIncCommitLogArg()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(release())) {
  }
}

int ObCallRemoteWriteDDLIncCommitLogArg::init(const share::ObLSID &ls_id,
                                             const common::ObTabletID tablet_id,
                                             const common::ObTabletID lob_meta_tablet_id,
                                             transaction::ObTxDesc *tx_desc)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid() || !tablet_id.is_valid() ||
                  OB_ISNULL(tx_desc) || !tx_desc->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet id is not valid", K(ret), K(ls_id), K(tablet_id), K(lob_meta_tablet_id), KPC(tx_desc));
  } else if (OB_FAIL(release())) {
  } else {
    
    ls_id_ = ls_id;
    tablet_id_ = tablet_id;
    lob_meta_tablet_id_ = lob_meta_tablet_id;
    tx_desc_ = tx_desc;
  }
  return ret;
}

int ObCallRemoteWriteDDLIncCommitLogArg::release()
{
  int ret = OB_SUCCESS;
  if (tx_desc_ != nullptr && need_release_) {
    ObTransService *tx_svc = MTL_WITH_CHECK(ObTransService *);
    if (OB_ISNULL(tx_svc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", K(ret));
    } else if (OB_FAIL(tx_svc->release_tx(*tx_desc_))) {
    } else {
      need_release_ = false;
      tx_desc_ = nullptr;
    }
  }

  return ret;
}

OB_DEF_SERIALIZE(ObCallRemoteWriteDDLIncCommitLogArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, ls_id_, tablet_id_, lob_meta_tablet_id_);
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(tx_desc_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tx_desc_ is nullptr", K(ret));
    } else {
      LST_DO_CODE(OB_UNIS_ENCODE, *tx_desc_);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObCallRemoteWriteDDLIncCommitLogArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, ls_id_, tablet_id_, lob_meta_tablet_id_);
  if (OB_SUCC(ret)) {
    ObTransService *tx_svc = nullptr;
    if (OB_FAIL(release())) {
    } else if (OB_ISNULL(tx_svc = MTL_WITH_CHECK(ObTransService *))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", KR(ret));
    } else if (OB_FAIL(tx_svc->acquire_tx(buf, data_len, pos, tx_desc_))) {
    } else {
      need_release_ = true;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObCallRemoteWriteDDLIncCommitLogArg)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, ls_id_, tablet_id_, lob_meta_tablet_id_);
  if (tx_desc_ != nullptr) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, *tx_desc_);
  }
  return len;
}

OB_SERIALIZE_MEMBER(ObCallRemoteWriteDDLIncCommitLogRes, tx_result_);

ObRegisterTxDataArg::ObRegisterTxDataArg()
  : tx_desc_(nullptr),
    ls_id_(),
    type_(transaction::ObTxDataSourceType::UNKNOWN),
    buf_(),
    seq_no_(),
    request_id_(0),
    register_flag_()
{
}


int ObRegisterTxDataArg::init(const ObTxDesc &tx_desc,
                              const ObLSID &ls_id,
                              const ObTxDataSourceType &type,
                              const ObString &buf,
                              const transaction::ObTxSEQ seq_no,
                              const int64_t base_request_id,
                              const transaction::ObRegisterMdsFlag &register_flag)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(false || !tx_desc.is_valid() || !ls_id.is_valid()
                  || type == ObTxDataSourceType::UNKNOWN || !seq_no.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tx_desc), K(ls_id), K(type), K(seq_no));
  } else {
    
    tx_desc_ = const_cast<ObTxDesc *>(&tx_desc);
    ls_id_ = ls_id;
    type_ = type;
    buf_ = buf;
    seq_no_ = seq_no;
    request_id_ = base_request_id;
    register_flag_ = register_flag;
  }
  return ret;
}


void ObRegisterTxDataArg::inc_request_id(const int64_t base_request_id)
{
  if (-1 != base_request_id) {
    request_id_ = base_request_id + 1;
  } else {
    request_id_++;
  }
}

OB_DEF_SERIALIZE(ObRegisterTxDataArg)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(*tx_desc_);
  LST_DO_CODE(OB_UNIS_ENCODE, ls_id_, type_, buf_, request_id_, register_flag_, seq_no_);
  return ret;
}
OB_DEF_DESERIALIZE(ObRegisterTxDataArg)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)) {
    ObTransService *tx_svc = MTL_WITH_CHECK(ObTransService *);
    if (OB_ISNULL(tx_svc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null tx service ptr", KR(ret));
    } else if (OB_FAIL(tx_svc->acquire_tx(buf, data_len, pos, tx_desc_))) {
    } else {
      LST_DO_CODE(OB_UNIS_DECODE, ls_id_, type_, buf_, request_id_, register_flag_, seq_no_);
      LOG_INFO("deserialize txDesc from session", KPC_(tx_desc), KPC(this));
    }
  }
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObRegisterTxDataArg)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(*tx_desc_);
  LST_DO_CODE(OB_UNIS_ADD_LEN, ls_id_, type_, buf_, request_id_, register_flag_, seq_no_);
  return len;
}



void ObRegisterTxDataResult::reset()
{
  result_ = OB_SUCCESS;
  tx_result_.reset();
  return;
}

OB_SERIALIZE_MEMBER(ObRegisterTxDataResult, result_, tx_result_);

OB_SERIALIZE_MEMBER(ObSwitchSchemaResult, ret_);

int ObTenantConfigArg::assign(const ObTenantConfigArg &other)
{
  int ret = OB_SUCCESS;
  
  config_str_ = other.config_str_;
  return ret;
}

OB_SERIALIZE_MEMBER(ObTenantConfigArg, config_str_);











OB_SERIALIZE_MEMBER(ObFlushOptStatArg, is_flush_col_usage_, is_flush_dml_stat_);

ObCancelDDLTaskArg::ObCancelDDLTaskArg()
  : task_id_()
{
}

ObCancelDDLTaskArg::ObCancelDDLTaskArg(const ObCurTraceId::TraceId &task_id)
  : task_id_(task_id)
{
}



OB_SERIALIZE_MEMBER(ObCancelDDLTaskArg, task_id_);

int ObEstBlockArgElement::assign(const ObEstBlockArgElement &other)
{
  int ret = OB_SUCCESS;
  
  tablet_id_ = other.tablet_id_;
  ls_id_ = other.ls_id_;
  return column_group_ids_.assign(other.column_group_ids_);
}

OB_SERIALIZE_MEMBER(ObEstBlockArgElement, tablet_id_, ls_id_, column_group_ids_);



OB_SERIALIZE_MEMBER(ObEstBlockArg, tablet_params_arg_);

int ObEstBlockResElement::assign(const ObEstBlockResElement &other)
{
  int ret = OB_SUCCESS;
  macro_block_count_ = other.macro_block_count_;
  micro_block_count_ = other.micro_block_count_;
  sstable_row_count_ = other.sstable_row_count_;
  memtable_row_count_ = other.memtable_row_count_;
  if (OB_FAIL(cg_macro_cnt_arr_.assign(other.cg_macro_cnt_arr_))) {
  } else if (OB_FAIL(cg_micro_cnt_arr_.assign(other.cg_micro_cnt_arr_))) {
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObEstBlockResElement, macro_block_count_, micro_block_count_,
    sstable_row_count_, memtable_row_count_, cg_macro_cnt_arr_, cg_micro_cnt_arr_);


OB_SERIALIZE_MEMBER(ObEstBlockRes, tablet_params_res_);

int ObEstSkipRateArgElement::assign(const ObEstSkipRateArgElement &other)
{
  int ret = OB_SUCCESS;
  
  table_id_ = other.table_id_;
  tablet_id_ = other.tablet_id_;
  ls_id_ = other.ls_id_;
  if (OB_FAIL(sample_count_.assign(other.sample_count_))) {
  } else if (OB_FAIL(column_ids_.assign(other.column_ids_))) {
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObEstSkipRateArgElement, table_id_, tablet_id_, ls_id_, sample_count_, column_ids_);


OB_SERIALIZE_MEMBER(ObEstSkipRateArg, tablet_params_arg_);

int ObEstSkipRateResElement::assign(const ObEstSkipRateResElement &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(cg_skip_rate_arr_.assign(other.cg_skip_rate_arr_))) {
  } else if (OB_FAIL(sample_count_.assign(other.sample_count_))) {
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObEstSkipRateResElement, cg_skip_rate_arr_, sample_count_);


OB_SERIALIZE_MEMBER(ObEstSkipRateRes, tablet_params_res_);

OB_SERIALIZE_MEMBER(ObBatchGetTabletAutoincSeqArg, ls_id_, src_tablet_ids_, dest_tablet_ids_);

int ObBatchGetTabletAutoincSeqArg::assign(const ObBatchGetTabletAutoincSeqArg &other)
{
  int ret = OB_SUCCESS;
  
  ls_id_ = other.ls_id_;
  if (OB_FAIL(src_tablet_ids_.assign(other.src_tablet_ids_))) {
  } else if (OB_FAIL(dest_tablet_ids_.assign(other.dest_tablet_ids_))) {
  }
  return ret;
}

int ObBatchGetTabletAutoincSeqArg::init(const share::ObLSID &ls_id, const ObIArray<share::ObMigrateTabletAutoincSeqParam> &params)
{
  int ret = OB_SUCCESS;
  
  ls_id_ = ls_id;
  src_tablet_ids_.reset();
  dest_tablet_ids_.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
    const ObMigrateTabletAutoincSeqParam &param = params.at(i);
    if (OB_FAIL(src_tablet_ids_.push_back(param.src_tablet_id_))) {
    } else if (OB_FAIL(dest_tablet_ids_.push_back(param.dest_tablet_id_))) {
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(*this));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObBatchGetTabletAutoincSeqRes, autoinc_params_);


OB_SERIALIZE_MEMBER(ObBatchSetTabletAutoincSeqArg, ls_id_, autoinc_params_, is_tablet_creating_);

int ObBatchSetTabletAutoincSeqArg::assign(const ObBatchSetTabletAutoincSeqArg &other)
{
  int ret = OB_SUCCESS;
  
  ls_id_ = other.ls_id_;
  is_tablet_creating_ = other.is_tablet_creating_;
  if (OB_FAIL(autoinc_params_.assign(other.autoinc_params_))) {
  }
  return ret;
}

int ObBatchSetTabletAutoincSeqArg::init(const share::ObLSID &ls_id, const ObIArray<share::ObMigrateTabletAutoincSeqParam> &params)
{
  int ret = OB_SUCCESS;
  
  ls_id_ = ls_id;
  autoinc_params_.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
    const ObMigrateTabletAutoincSeqParam &param = params.at(i);
    if (OB_FAIL(autoinc_params_.push_back(param))) {
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(*this));
  }
  return ret;
}

void ObBatchSetTabletAutoincSeqArg::reset()
{
  
  ls_id_.reset();
  autoinc_params_.reset();
  is_tablet_creating_ = false;
  return;
}

OB_SERIALIZE_MEMBER(ObBatchSetTabletAutoincSeqRes, autoinc_params_);


OB_SERIALIZE_MEMBER(ObBatchGetTabletBindingArg, ls_id_, tablet_ids_, check_committed_);


int ObBatchGetTabletBindingArg::init(const share::ObLSID &ls_id, const ObIArray<ObTabletID> &tablet_ids, const bool check_committed)
{
  int ret = OB_SUCCESS;
  
  ls_id_ = ls_id;
  check_committed_ = check_committed;
  if (OB_FAIL(tablet_ids_.assign(tablet_ids))) {
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(*this));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObBatchGetTabletBindingRes, binding_datas_);


OB_SERIALIZE_MEMBER(ObBatchGetTabletSplitArg, ls_id_, tablet_ids_, check_committed_);


int ObBatchGetTabletSplitArg::init(const share::ObLSID &ls_id, const ObIArray<ObTabletID> &tablet_ids, const bool check_committed)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid() || tablet_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ls_id), K(tablet_ids), K(check_committed));
  } else if (OB_FAIL(tablet_ids_.assign(tablet_ids))) {
  } else {
    
    ls_id_ = ls_id;
    check_committed_ = check_committed;
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(*this));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObBatchGetTabletSplitRes, split_datas_);

OB_SERIALIZE_MEMBER(ObSessInfoVerifyArg, sess_id_, proxy_sess_id_);

bool ObSessionInfoVeriRes::is_valid() const
{
  return true;
}

OB_DEF_SERIALIZE(ObSessionInfoVeriRes)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE,
          verify_info_buf_,
          need_verify_);
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObSessionInfoVeriRes)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)) {
    ObString tmp_string;
    char *tmp_ptr = NULL;

    if (OB_FAIL(tmp_string.deserialize(buf, data_len, pos))) {
    } else if (OB_ISNULL(tmp_ptr = (char *)allocator_.alloc(tmp_string.length()))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory!", K(ret));
    } else {
      MEMCPY(tmp_ptr, tmp_string.ptr(), tmp_string.length());
      verify_info_buf_.assign_ptr(tmp_ptr, tmp_string.length());
      tmp_string.reset();
    }
    if (OB_FAIL(ret)) {
      allocator_.free(tmp_ptr);
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE,
          need_verify_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObSessionInfoVeriRes)
{
  int64_t len = 0;
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    LST_DO_CODE(OB_UNIS_ADD_LEN,
          verify_info_buf_,
          need_verify_);
  }
  if (OB_FAIL(ret)) {
    len = -1;
  }
  return len;
}

bool ObKillClientSessionArg::is_valid() const
{
  return true;
}


bool ObKillQueryClientSessionArg::is_valid() const
{
  return true;
}

OB_SERIALIZE_MEMBER(ObKillClientSessionArg, create_time_, client_sess_id_);
OB_SERIALIZE_MEMBER(ObKillClientSessionRes, can_kill_client_sess_);
OB_SERIALIZE_MEMBER(ObKillQueryClientSessionArg, client_sess_id_);

OB_SERIALIZE_MEMBER(ObClientSessionCreateTimeAndAuthArg, client_sess_id_, user_id_, has_user_super_privilege_);
OB_SERIALIZE_MEMBER(ObClientSessionCreateTimeAndAuthRes, client_sess_create_time_, have_kill_auth_);

OB_SERIALIZE_MEMBER(ObInitTenantConfigArg, tenant_configs_);

int ObInitTenantConfigArg::assign(const ObInitTenantConfigArg &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
  } else if (OB_FAIL(tenant_configs_.assign(other.tenant_configs_))) {
      }
  return ret;
}


int ObInitTenantConfigArg::add_tenant_config(const ObTenantConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tenant_configs_.push_back(arg))) {
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObInitTenantConfigRes, ret_);

OB_SERIALIZE_MEMBER((ObRecompileAllViewsBatchArg, ObDDLArg),
                    view_ids_);


OB_SERIALIZE_MEMBER((ObCatalogDDLArg, ObDDLArg), schema_, ddl_type_, if_not_exist_, if_exist_, user_id_);


DEF_TO_STRING(ObCreateCCLRuleArg)
{
  int64_t pos = 0;
  J_KV(K_(if_not_exist),
       K_(affect_databases_name),
       K_(affect_tables_name),
       K_(ccl_rule_schema));
  return pos;
}

OB_SERIALIZE_MEMBER((ObCreateCCLRuleArg, ObDDLArg),
                    if_not_exist_,
                    affect_databases_name_,
                    affect_tables_name_,
                    ccl_rule_schema_);


DEF_TO_STRING(ObDropCCLRuleArg)
{
  int64_t pos = 0;
  J_KV(K_(if_exist),
       K_(ccl_rule_name));
  return pos;
}

OB_SERIALIZE_MEMBER((ObDropCCLRuleArg, ObDDLArg),
                    if_exist_,
                    
                    ccl_rule_name_);


OB_SERIALIZE_MEMBER(ObGetServerResourceInfoArg, rs_addr_);

int ObGetServerResourceInfoArg::init(const common::ObAddr &rs_addr)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!rs_addr.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rs_addr", KR(ret), K(rs_addr));
  } else {
    rs_addr_ = rs_addr;
  }
  return ret;
}

int ObGetServerResourceInfoArg::assign(const ObGetServerResourceInfoArg &other)
{
  int ret = OB_SUCCESS;
  rs_addr_ = other.rs_addr_;
  return ret;
}

bool ObGetServerResourceInfoArg::is_valid() const
{
  return rs_addr_.is_valid();
}

void ObGetServerResourceInfoArg::reset()
{
  rs_addr_.reset();
}

OB_SERIALIZE_MEMBER(ObGetServerResourceInfoResult, server_, resource_info_);

int ObGetServerResourceInfoResult::init(
    const common::ObAddr &server,
    const share::ObServerResourceInfo &resource_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!server.is_valid() || !resource_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server or resource_info", KR(ret), K(server), K(resource_info));
  } else {
    server_ = server;
    resource_info_ = resource_info;
  }
  return ret;
}

int ObGetServerResourceInfoResult::assign(const ObGetServerResourceInfoResult &other)
{
  int ret = OB_SUCCESS;
  server_ = other.server_;
  resource_info_ = other.resource_info_;
  return ret;
}

bool ObGetServerResourceInfoResult::is_valid() const
{
  return server_.is_valid() && resource_info_.is_valid();
}

void ObGetServerResourceInfoResult::reset()
{
  server_.reset();
  resource_info_.reset();
}

OB_SERIALIZE_MEMBER(ObBroadcastConsensusVersionArg, consensus_version_);
bool ObBroadcastConsensusVersionArg::is_valid() const
{
  return true && OB_INVALID_VERSION != consensus_version_;
}


int ObBroadcastConsensusVersionArg::assign(const ObBroadcastConsensusVersionArg &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    
    consensus_version_ = other.consensus_version_;
  }
  return ret;
}
OB_SERIALIZE_MEMBER(ObTTLResponseArg, task_id_, server_addr_, task_status_, err_code_);
ObTTLResponseArg::ObTTLResponseArg()
    : task_id_(OB_INVALID_ID),
      server_addr_(),
      task_status_(15),
      err_code_(OB_SUCCESS)
{}


OB_SERIALIZE_MEMBER(ObSeqCleanCacheRes, inited_, with_prefetch_node_, cache_node_, prefetch_node_);

ObSeqCleanCacheRes::ObSeqCleanCacheRes()
    : inited_(false), with_prefetch_node_(false), cache_node_(), prefetch_node_()
{
}


OB_SERIALIZE_MEMBER(ObTTLRequestArg, cmd_code_, trigger_type_, task_id_);

int ObTTLRequestArg::assign(const ObTTLRequestArg &other)
{
  int ret = OB_SUCCESS;

  cmd_code_ = other.cmd_code_;
  task_id_ = other.task_id_;
  
  trigger_type_ = other.trigger_type_;

  return ret;
}










OB_SERIALIZE_MEMBER(ObCancelGatherStatsArg, task_id_);















OB_SERIALIZE_MEMBER(ObCheckNestedMViewMdsArg, mview_id_, refresh_id_, target_data_sync_scn_);
OB_SERIALIZE_MEMBER(ObCheckNestedMViewMdsRes, target_data_sync_scn_, ret_);

OB_SERIALIZE_MEMBER((ObCreateTableGroupRes, ObParallelDDLRes), tablegroup_id_);



OB_SERIALIZE_MEMBER((ObCreateAiModelArg, ObDDLArg), model_info_);
OB_SERIALIZE_MEMBER((ObDropAiModelArg, ObDDLArg), ai_model_name_);

int ObCreateAiModelArg::check_valid() const
{
  int ret = OB_SUCCESS;
  if (false) {
    return OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id");
  } else if (OB_FAIL(model_info_.check_valid())) {
  }
  return ret;
}

int ObCreateAiModelArg::assign(const ObCreateAiModelArg &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
  } else if (OB_FAIL(ObDDLArg::assign(other))) {
  } else {
    model_info_ = other.model_info_;
  }
  return ret;
}

int ObDropAiModelArg::assign(const ObDropAiModelArg &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
  } else if (OB_FAIL(ObDDLArg::assign(other))) {
  } else {
    ai_model_name_ = other.ai_model_name_;
  }
  return ret;
}

bool ObRevokeObjMysqlArg::is_valid() const
{
  return OB_INVALID_ID != user_id_
      && !obj_name_.empty();
}

int ObRevokeObjMysqlArg::assign(const ObRevokeObjMysqlArg& other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::assign(other))) {
  } else {
    
    user_id_ = other.user_id_;
    obj_name_ = other.obj_name_;
    priv_set_ = other.priv_set_;
    grant_ = other.grant_;
    obj_type_ = other.obj_type_;
    grantor_ = other.grantor_;
    grantor_host_ = other.grantor_host_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObRevokeObjMysqlArg, ObDDLArg),
                    
                    user_id_,
                    obj_name_,
                    obj_type_,
                    priv_set_,
                    grant_,
                    grantor_,
                    grantor_host_);

int ObCreateLocationArg::assign(const ObCreateLocationArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::assign(other))) {
  } else if (OB_FAIL(schema_.assign(other.schema_))) {
  } else {
    or_replace_ = other.or_replace_;
    user_id_ = other.user_id_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObCreateLocationArg, ObDDLArg), or_replace_, user_id_, schema_);

int ObDropLocationArg::assign(const ObDropLocationArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLArg::assign(other))) {
  } else {
    
    location_name_ = other.location_name_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObDropLocationArg, ObDDLArg), location_name_);





}//end namespace obcall
}//end namespace oceanbase
