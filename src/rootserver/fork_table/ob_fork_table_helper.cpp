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

#define USING_LOG_PREFIX RS

#include "rootserver/fork_table/ob_fork_table_helper.h"
#include "rootserver/ob_ddl_operator.h"
#include "share/ob_fork_table_util.h"
#include "share/tablet/ob_tablet_to_ls_operator.h"
#include "share/ob_autoincrement_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tablet/ob_tablet_fork_mds_helper.h"
#include "rootserver/truncate_info/ob_truncate_info_service.h"
#include "storage/truncate_info/ob_truncate_info.h"
#include "storage/truncate_info/ob_truncate_info_array.h"
#include "observer/ob_inner_sql_connection.h"
#include "lib/mysqlclient/ob_isql_connection.h"
#include "storage/tx/ob_ts_mgr.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/schema/ob_schema_utils.h"

namespace oceanbase
{
namespace rootserver
{


int ObForkTableHelper::init(const common::ObIArray<share::schema::ObTableSchema> &table_schemas)
{
  int ret = OB_SUCCESS;

  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("fork table helper init twice", KR(ret), K(tenant_id_), K(fork_table_info_));
  } else if (!fork_table_info_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fork table info is invalid", KR(ret), K(fork_table_info_));
  } else if (table_schemas.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table schemas is empty", KR(ret));
  } else if (FALSE_IT(src_table_id_ = fork_table_info_.get_fork_src_table_id())) {
  } else if (OB_FAIL(schema_guard_.get_table_schema(tenant_id_, src_table_id_, src_table_schema_))) {
    LOG_WARN("failed to get source table schema", KR(ret), K(tenant_id_), K(fork_table_info_.get_fork_src_table_id()));
  } else if (OB_ISNULL(src_table_schema_)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("source table not exist", KR(ret), K(fork_table_info_.get_fork_src_table_id()));
  } else if (OB_ISNULL(dst_table_schema_ = &table_schemas.at(0))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dst table schema is null", KR(ret));
  } else if (FALSE_IT(dst_table_id_ = dst_table_schema_->get_table_id())) {
  } else if (OB_FAIL(share::ObForkTableUtil::collect_tablet_ids_from_table(schema_guard_, tenant_id_, *src_table_schema_, src_tablet_ids_))) {
    LOG_WARN("failed to collect src tablet ids", KR(ret), K(*src_table_schema_));
  } else {
    ObSEArray<int64_t, 16> sorted_indices;
    for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); ++i) {
      if (OB_FAIL(sorted_indices.push_back(i))) {
        LOG_WARN("failed to push index", KR(ret));
      }
    }
    if (OB_SUCC(ret)) {
      struct TableIdLess
      {
        explicit TableIdLess(const common::ObIArray<share::schema::ObTableSchema> &schemas)
          : schemas_(schemas)
        {}
        bool operator()(const int64_t a, const int64_t b) const
        {
          return schemas_.at(a).get_table_id() < schemas_.at(b).get_table_id();
        }
        const common::ObIArray<share::schema::ObTableSchema> &schemas_;
      };
      lib::ob_sort(sorted_indices.begin(), sorted_indices.end(), TableIdLess(table_schemas));
      for (int64_t i = 0; OB_SUCC(ret) && i < sorted_indices.count(); ++i) {
        const share::schema::ObTableSchema &schema = table_schemas.at(sorted_indices.at(i));
        if (OB_FAIL(schema.get_tablet_ids(dst_tablet_ids_))) {
          LOG_WARN("failed to get tablet ids from table schema", KR(ret), K(schema.get_table_id()));
        }
      }
    }
    if (OB_SUCC(ret) && src_tablet_ids_.count() != dst_tablet_ids_.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet count mismatch", KR(ret), K(src_tablet_ids_.count()), K(dst_tablet_ids_.count()));
    }
    if (OB_SUCC(ret)) {
      inited_ = true;
    }
  }
  return ret;
}

int ObForkTableHelper::execute()
{
  int ret = OB_SUCCESS;

  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("fork table helper not init", KR(ret));
  } else {
    MTL_SWITCH(tenant_id_) {
      if (OB_FAIL(copy_tablet_autoinc_seq_info_())) {
        LOG_WARN("failed to copy tablet autoinc seq", KR(ret), K(src_table_id_), K(dst_table_id_));
      } else if (OB_FAIL(copy_tablet_truncate_info_())) {
        LOG_WARN("failed to copy tablet truncate info", KR(ret), K(src_table_id_), K(dst_table_id_));
      } else if (OB_FAIL(copy_table_autoinc_seq_info_())) {
        LOG_WARN("failed to copy table autoinc info", KR(ret), K(src_table_id_), K(dst_table_id_));
      } else {
        LOG_INFO("fork table: successfully executed fork table helper",
            K(tenant_id_), K(fork_table_info_), K(src_table_id_), K(dst_table_id_),
            "tablet_cnt", src_tablet_ids_.count());
      }
    }
  }

  return ret;
}

int ObForkTableHelper::copy_tablet_autoinc_seq_info_()
{
  int ret = OB_SUCCESS;
  ObSEArray<share::ObMigrateTabletAutoincSeqParam, 4> autoinc_params;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("fork table helper not init", KR(ret));
  } else {
    ObArenaAllocator allocator("ForkAutoinc");
    obrpc::ObBatchSetTabletAutoincSeqArg arg;
    arg.tenant_id_ = tenant_id_;
    arg.ls_id_ = SYS_LS;
    arg.is_tablet_creating_ = true;

    for (int64_t i = 0; OB_SUCC(ret) && i < src_tablet_ids_.count(); ++i) {
      allocator.reuse();
      const ObTabletID &src_tablet_id = src_tablet_ids_.at(i);
      const ObTabletID &dst_tablet_id = dst_tablet_ids_.at(i);
      ObTabletHandle tablet_handle;
      ObTabletAutoincSeq autoinc_seq;
      share::ObMigrateTabletAutoincSeqParam param;
      param.src_tablet_id_ = src_tablet_id;
      param.dest_tablet_id_ = dst_tablet_id;
      param.ret_code_ = OB_SUCCESS;

      if (OB_FAIL(get_tablet_handle_(src_tablet_id, tablet_handle))) {
        LOG_WARN("failed to get source tablet", K(ret), K(src_tablet_id));
      } else if (OB_ISNULL(tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet handle is null", K(ret), K(src_tablet_id));
      } else if (OB_FAIL(tablet_handle.get_obj()->get_autoinc_seq(autoinc_seq, allocator))) {
        LOG_WARN("failed to get autoinc seq", K(ret), K(src_tablet_id));
      } else if (OB_FAIL(autoinc_seq.get_autoinc_seq_value(param.autoinc_seq_))) {
        LOG_WARN("failed to get autoinc seq value", K(ret), K(src_tablet_id));
      } else if (OB_FAIL(arg.autoinc_params_.push_back(param))) {
        LOG_WARN("failed to push autoinc param", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      storage::ObTabletForkMdsArg fork_mds_arg;
      fork_mds_arg.tenant_id_ = tenant_id_;
      fork_mds_arg.ls_id_ = SYS_LS;
      if (OB_FAIL(fork_mds_arg.set_autoinc_seq_arg(arg))) {
        LOG_WARN("failed to set autoinc seq arg", K(ret), K(arg));
      } else if (OB_FAIL(storage::ObTabletForkMdsHelper::register_mds(fork_mds_arg, false, trans_))) {
        LOG_WARN("failed to register fork mds", K(ret), K(SYS_LS));
      } else {
        LOG_INFO("fork table: successfully registered fork mds for autoinc seq",
                 K(SYS_LS), K(arg.autoinc_params_.count()));
      }
    }
  }

  return ret;
}

int ObForkTableHelper::copy_tablet_truncate_info_()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("fork table helper not init", KR(ret));
  } else {
    ObArenaAllocator allocator("ForkTruncate");
    ObTruncateInfoArray truncate_info_array;
    share::SCN max_readable_scn;
    storage::ObTruncateInfo *latest_truncate_info = nullptr;
    int64_t empty_cnt = 0;
    int64_t registered_cnt = 0;

    if (OB_FAIL(OB_TS_MGR.get_ts_sync(tenant_id_, GCONF.rpc_timeout, max_readable_scn))) {
      LOG_WARN("failed to get gts", K(ret), K(tenant_id_));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < src_tablet_ids_.count(); ++i) {
      allocator.reuse();
      const ObTabletID &src_tablet_id = src_tablet_ids_.at(i);
      const ObTabletID &dst_tablet_id = dst_tablet_ids_.at(i);
      ObTabletHandle src_tablet_handle;
      truncate_info_array.reset();

      if (OB_FAIL(get_tablet_handle_(src_tablet_id, src_tablet_handle))) {
        LOG_WARN("failed to get source tablet", K(ret), K(src_tablet_id));
      } else if (OB_ISNULL(src_tablet_handle.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet handle is null", K(ret), K(src_tablet_id));
      } else if (OB_FAIL(src_tablet_handle.get_obj()->read_truncate_info_array(
          allocator,
          common::ObVersionRange(src_tablet_handle.get_obj()->get_last_major_snapshot_version(), max_readable_scn.get_val_for_tx()),
          false /*for_access*/,
          truncate_info_array))) {
        LOG_WARN("failed to read truncate info array", K(ret), K(src_tablet_id));
      } else if (truncate_info_array.empty()) {
        ++empty_cnt;
        LOG_DEBUG("fork table: no truncate info in source tablet", K(src_tablet_id), K(dst_tablet_id));
      } else if (OB_ISNULL(latest_truncate_info = truncate_info_array.at(truncate_info_array.count() - 1))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get latest truncate info", K(ret), K(src_tablet_id));
      } else {
        storage::ObTabletForkMdsArg fork_mds_arg;
        fork_mds_arg.tenant_id_ = tenant_id_;
        fork_mds_arg.ls_id_ = SYS_LS;
        rootserver::ObTruncateTabletArg truncate_arg;
        truncate_arg.ls_id_ = SYS_LS;
        truncate_arg.index_tablet_id_ = dst_tablet_id;
        if (OB_FAIL(truncate_arg.truncate_info_.assign(allocator, *latest_truncate_info))) {
          LOG_WARN("fail to assign truncate info", K(ret), K(*latest_truncate_info));
        } else if (OB_UNLIKELY(!truncate_arg.is_valid())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("truncate arg is invalid", K(ret), K(truncate_arg));
        } else if (OB_FAIL(fork_mds_arg.set_truncate_arg(truncate_arg))) {
          LOG_WARN("failed to set truncate arg", K(ret), K(truncate_arg));
        } else if (OB_FAIL(storage::ObTabletForkMdsHelper::register_mds(fork_mds_arg, false/*need_flush_redo*/, trans_))) {
          LOG_WARN("failed to register fork mds for truncate info", K(ret), K(SYS_LS), K(dst_tablet_id));
        } else {
          ++registered_cnt;
          LOG_DEBUG("fork table: successfully registered truncate info for tablet",
              K(src_tablet_id), K(dst_tablet_id), K(latest_truncate_info->key_));
        }
      }
    }
    if (OB_SUCC(ret)) {
      LOG_INFO("fork table: truncate info copy summary", K(src_table_id_), K(dst_table_id_),
          "tablet_cnt", src_tablet_ids_.count(), K(empty_cnt), K(registered_cnt));
    }
  }

  return ret;
}

int ObForkTableHelper::copy_table_autoinc_seq_info_()
{
  int ret = OB_SUCCESS;
  ObDDLOperator ddl_operator(schema_service_, sql_proxy_);
  const uint64_t src_autoinc_column_id = src_table_schema_->get_autoinc_column_id();
  const uint64_t dst_autoinc_column_id = dst_table_schema_->get_autoinc_column_id();

  if (OB_INVALID_ID == src_autoinc_column_id || 0 == src_autoinc_column_id) {
    LOG_INFO("fork table: source table has no auto increment column", K(src_table_id_));
  } else if (OB_INVALID_ID == dst_autoinc_column_id || 0 == dst_autoinc_column_id) {
    LOG_INFO("fork table: destination table has no auto increment column", K(dst_table_schema_->get_table_id()));
  } else if (src_autoinc_column_id != dst_autoinc_column_id) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("autoinc column id mismatch", KR(ret), K(src_autoinc_column_id), K(dst_autoinc_column_id));
  } else {
    const bool is_order_mode = src_table_schema_->is_order_auto_increment_mode();
    const int64_t autoinc_version = src_table_schema_->get_truncate_version();
    uint64_t src_sequence_value = 0;
    share::ObAutoincrementService &autoinc_service = share::ObAutoincrementService::get_instance();

    if (OB_FAIL(autoinc_service.get_sequence_value(
            tenant_id_, src_table_id_, src_autoinc_column_id,
            is_order_mode, autoinc_version, src_sequence_value))) {
      if (OB_ENTRY_NOT_EXIST == ret || OB_SCHEMA_ERROR == ret) {
        ret = OB_SUCCESS;
        LOG_INFO("fork table: source table has no auto increment record",
                 K(src_table_id_), K(is_order_mode));
      } else {
        LOG_WARN("failed to get auto increment sequence value from ObAutoincrementService",
                 K(ret), K(src_table_id_), K(src_autoinc_column_id), K(is_order_mode));
      }
    } else {
      LOG_INFO("fork table: got sequence value from ObAutoincrementService (read-only)",
               K(src_table_id_), K(src_autoinc_column_id), K(src_sequence_value), K(is_order_mode));
    }

    if (OB_SUCC(ret) && src_sequence_value > 0) {
      const uint64_t sync_value = src_sequence_value - 1;
      if (OB_FAIL(ddl_operator.set_target_auto_inc_sync_value(
              tenant_id_, dst_table_schema_->get_table_id(), dst_autoinc_column_id,
              src_sequence_value, sync_value, trans_))) {
        LOG_WARN("failed to set auto increment sequence value to destination table",
                 K(ret), K(dst_table_schema_->get_table_id()), K(dst_autoinc_column_id),
                 K(src_sequence_value), K(sync_value));
      } else {
        LOG_INFO("fork table: successfully copied table autoinc info",
                 K(src_table_id_), K(dst_table_schema_->get_table_id()), K(src_autoinc_column_id),
                 K(src_sequence_value), K(sync_value));
      }
    }
  }

  return ret;
}

// TODO(fankun.fan): copy table statistics
int ObForkTableHelper::copy_table_statistics_()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("fork table helper not init", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < src_tablet_ids_.count(); ++i) {
      const ObTabletID &src_tablet_id = src_tablet_ids_.at(i);
      const ObTabletID &dst_tablet_id = dst_tablet_ids_.at(i);
      int64_t src_part_id = OB_INVALID_ID;
      int64_t src_subpart_id = OB_INVALID_ID;
      int64_t dst_part_id = OB_INVALID_ID;
      int64_t dst_subpart_id = OB_INVALID_ID;

      if (PARTITION_LEVEL_ZERO == src_table_schema_->get_part_level()) {
        src_part_id = src_table_schema_->get_object_id();
        dst_part_id = dst_table_schema_->get_object_id();
      } else {
        if (OB_FAIL(src_table_schema_->get_part_id_by_tablet(src_tablet_id, src_part_id, src_subpart_id))) {
          LOG_WARN("failed to get src partition id by tablet", K(ret), K(src_tablet_id));
        } else if (OB_FAIL(dst_table_schema_->get_part_id_by_tablet(dst_tablet_id, dst_part_id, dst_subpart_id))) {
          LOG_WARN("failed to get dst partition id by tablet", K(ret), K(dst_tablet_id));
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_INVALID_ID == src_part_id || OB_INVALID_ID == dst_part_id) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid partition id", K(ret), K(src_part_id), K(dst_part_id), K(src_table_id_), K(dst_table_id_));
        } else if (OB_FAIL(copy_stat_info_(OB_ALL_TABLE_STAT_TNAME,
                                            src_table_id_, src_part_id,
                                            dst_table_id_, dst_part_id))) {
          LOG_WARN("failed to copy table stat", K(ret), K(src_table_id_), K(src_part_id), K(dst_table_id_), K(dst_part_id));
        } else if (OB_FAIL(copy_stat_info_(OB_ALL_COLUMN_STAT_TNAME,
                                           src_table_id_, src_part_id,
                                           dst_table_id_, dst_part_id))) {
          LOG_WARN("failed to copy column stat", K(ret),
                    K(src_table_id_), K(src_part_id), K(dst_table_id_), K(dst_part_id));
        } else if (OB_FAIL(copy_stat_info_(OB_ALL_HISTOGRAM_STAT_TNAME,
                                           src_table_id_, src_part_id,
                                           dst_table_id_, dst_part_id))) {
          LOG_WARN("failed to copy histogram stat", K(ret), K(src_table_id_), K(src_part_id), K(dst_table_id_), K(dst_part_id));
        }
      }
    }

    if (OB_SUCC(ret)) {
      LOG_INFO("fork table: successfully copied all statistics", K(src_table_id_), K(dst_table_id_), K(src_tablet_ids_.count()));
    }
  }

  return ret;
}

// TODO(fankun.fan): copy table statistics
int ObForkTableHelper::copy_stat_info_(
    const char *table_name,
    const uint64_t src_table_id,
    const int64_t src_part_id,
    const uint64_t dst_table_id,
    const int64_t dst_part_id)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  const char *table_schema = get_table_schema_(table_name);
  ObSqlString sql_string;

  if (OB_ISNULL(table_name) || OB_ISNULL(table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parameter invalid", K(ret), KP(table_name), KP(table_schema));
  } else if (OB_FAIL(sql_string.assign_fmt(
      "REPLACE INTO %s (table_id, partition_id, %s) "
      "SELECT %lu, %ld, %s FROM %s "
      "WHERE tenant_id = %ld AND table_id = %lu AND partition_id = %ld",
      table_name, table_schema,
      dst_table_id, dst_part_id, table_schema, table_name,
      tenant_id_, src_table_id, src_part_id))) {
    LOG_WARN("failed to assign sql string", K(ret), K(table_name),
              K(src_table_id), K(src_part_id), K(dst_table_id), K(dst_part_id));
  } else if (OB_FAIL(trans_.write(tenant_id_, sql_string.ptr(), affected_rows))) {
    LOG_WARN("failed to copy statistics", K(ret), K(sql_string));
  } else if (OB_UNLIKELY(affected_rows < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected affected_rows", K(ret), K(affected_rows));
  }

  return ret;
}

// TODO(fankun.fan): copy table statistics
const char *ObForkTableHelper::get_table_schema_(const char *table_name)
{
  const char *ret_schema = nullptr;
  if (OB_NOT_NULL(table_name) && 0 == STRCMP(table_name, OB_ALL_TABLE_STAT_TNAME)) {
    ret_schema = "gmt_create, gmt_modified, tenant_id, object_type, last_analyzed, "
                 "sstable_row_cnt, sstable_avg_row_len, macro_blk_cnt, micro_blk_cnt, "
                 "memtable_row_cnt, memtable_avg_row_len, row_cnt, avg_row_len, global_stats, "
                 "user_stats, stattype_locked, stale_stats, spare1, spare2, spare3, spare4, "
                 "spare5, spare6, index_type";
  } else if (OB_NOT_NULL(table_name) && 0 == STRCMP(table_name, OB_ALL_COLUMN_STAT_TNAME)) {
    ret_schema = "gmt_create, gmt_modified, tenant_id, column_id, object_type, "
                 "last_analyzed, distinct_cnt, null_cnt, max_value, b_max_value, min_value, "
                 "b_min_value, avg_len, distinct_cnt_synopsis, distinct_cnt_synopsis_size, "
                 "sample_size, density, bucket_cnt, histogram_type, global_stats, user_stats, "
                 "spare1, spare2, spare3, spare4, spare5, spare6, cg_macro_blk_cnt, "
                 "cg_micro_blk_cnt, cg_skip_rate";
  } else if (OB_NOT_NULL(table_name) && 0 == STRCMP(table_name, OB_ALL_HISTOGRAM_STAT_TNAME)) {
    ret_schema = "gmt_create, gmt_modified, tenant_id, column_id, endpoint_num, "
                 "object_type, endpoint_normalized_value, endpoint_value, b_endpoint_value, "
                 "endpoint_repeat_cnt";
  }
  return ret_schema;
}

int ObForkTableHelper::get_tablet_handle_(
    const common::ObTabletID &tablet_id,
    storage::ObTabletHandle &tablet_handle) const
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = nullptr;
  ObLS *ls = nullptr;
  ObLSHandle ls_handle;

  MTL_SWITCH(tenant_id_) {
    if (OB_ISNULL(ls_service = MTL(ObLSService*))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls service is null", K(ret), K(tenant_id_));
    } else if (OB_FAIL(ls_service->get_ls(SYS_LS, ls_handle, ObLSGetMod::DDL_MOD))) {
      LOG_WARN("get ls failed", K(ret), K(SYS_LS));
    } else if (FALSE_IT(ls = ls_handle.get_ls())) {
    } else if (OB_ISNULL(ls)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls is null", K(ret), K(SYS_LS));
    } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle))) {
      LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
    }
  }

  return ret;
}

} // namespace rootserver
} // namespace oceanbase
