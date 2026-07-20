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

#ifndef OB_TEST_DML_COMMON_H_
#define OB_TEST_DML_COMMON_H_

#include <gtest/gtest.h>

#define protected public
#define private public

#include "lib/ob_define.h"
#include "lib/time/ob_time_utility.h"
#include "lib/container/ob_array.h"
#include "common/ob_tablet_id.h"
#include "lib/time/ob_clock_generator.h"
#include "common/object/ob_obj_type.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_storage_format.h"
#include "storage/access/ob_table_param.h"
#include "storage/mockcontainer/mock_ob_iterator.h"
#include "mtlenv/mock_tenant_module_env.h"
#include "storage/ls/ob_ls_tablet_service.h"
#include "storage/tx/ob_trans_define.h"
#include "storage/tx/ob_trans_service.h"

#undef private
#undef protected

namespace oceanbase
{
namespace storage
{
class TestTxCallback : public transaction::ObITxCallback
{
public:
  TestTxCallback() : committed_(false) {}
public:
  virtual void callback(int ret) override { UNUSED(ret); committed_ = true; }
  bool wait() { return !committed_; }
private:
  bool committed_;
};

class TestDmlCommon
{
public:
  static int create_ls(ObLS *&ls);

  static int build_table_param(
      const ObTableSchema &table_schema,
      const ObIArray<uint64_t> &output_column_ids,
      share::schema::ObTableParam &table_param);
  static int build_table_scan_param(
      const uint64_t tenant_id,
      ObTxReadSnapshot &read_snapshot,
      const share::schema::ObTableParam &table_param,
      ObTableScanParam &scan_param);
  static int build_table_scan_param(
      const uint64_t tenant_id,
      ObTransID &tx_id,
      const share::schema::ObTableParam &table_param,
      ObTableScanParam &scan_param);
  static int build_table_scan_param_base_(
      const uint64_t tenant_id,
      const share::schema::ObTableParam &table_param,
      bool read_latest,
      ObTableScanParam &scan_param);
  static void build_data_table_schema(
      const uint64_t tenant_id,
      share::schema::ObTableSchema &table_schema);
  static void build_index_table_schema(
      const uint64_t tenant_id,
      share::schema::ObTableSchema &table_schema);
  static int build_tx_desc(const uint64_t tenant_id, ObTxDesc *&tx_desc);
  static void build_tx_param(ObTxParam &tx_param);
  static void release_tx_desc(ObTxDesc &tx_desc);
public:
  static const uint64_t TX_EXPIRE_TIME_US = 120 * 1000 * 1000; // 120s
  static const uint64_t TEST_DATA_TABLE_ID = 50;
  static const uint64_t TEST_INDEX_TABLE_ID = 51;
  static constexpr const char *data_row_str =
      "bigint  bigint   bigint  var         var        dml          \n"
      "1       62       20      Houston     Rockets    T_DML_INSERT \n"
      "2       65       17      SanAntonio  Spurs      T_DML_INSERT \n"
      "3       58       24      Dallas      Mavericks  T_DML_INSERT \n"
      "4       51       31      LosAngeles  Lakers     T_DML_INSERT \n"
      "5       57       25      Phoenix     Suns       T_DML_INSERT \n"
      "6       32       50      NewJersey   Nets       T_DML_INSERT \n"
      "7       44       38      Miami       Heats      T_DML_INSERT \n"
      "8       21       61      Chicago     Bulls      T_DML_INSERT \n"
      "9       47       35      Cleveland   Cavaliers  T_DML_INSERT \n"
      "10      59       23      Detroit     Pistons    T_DML_INSERT \n"
      "11      40       42      Utah        Jazz       T_DML_INSERT \n"
      "12      50       32      Boston      Celtics    T_DML_INSERT \n";
};

int TestDmlCommon::create_ls(ObLS *&ls)
{
  int ret = OB_SUCCESS;
  ls = nullptr;

  ObLSService *ls_svr = MTL(ObLSService*);

  if (OB_FAIL(ls_svr->create_ls())) {
    STORAGE_LOG(WARN, "failed to create ls");
  } else if (OB_FAIL(ls_svr->get_ls(ls))) {
    STORAGE_LOG(WARN, "failed to get ls");
  }

  // check leader
  STORAGE_LOG(INFO, "check leader");
  ObRole role;
  for (int i = 0; OB_SUCC(ret) && i < 15; i++) {
    int64_t proposal_id = 0;
    if (OB_FAIL(ls->get_log_handler()->get_role(role, proposal_id))) {
      STORAGE_LOG(WARN, "failed to get role", K(ret));
    } else if (role == ObRole::LEADER) {
      break;
    }
    ::sleep(1);
  }

  if (OB_SUCC(ret) && OB_UNLIKELY(ObRole::LEADER != role)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "unexpected error, role is not leader", K(ret), K(role));
  }

  return ret;
}

int TestDmlCommon::build_table_param(
    const ObTableSchema &table_schema,
    const ObIArray<uint64_t> &output_column_ids,
    share::schema::ObTableParam &table_param)
{
  int ret = OB_SUCCESS;

  //use table schema as index schema
  if (OB_UNLIKELY(!table_schema.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid args", K(ret), K(table_schema));
  } else if (OB_FAIL(table_param.convert(table_schema, output_column_ids, false /* force_mysql_mode */))) {
    STORAGE_LOG(WARN, "failed to convert to table param", K(ret), K(table_schema), K(output_column_ids));
  }

  return ret;
}

/* table_scan read_latest of transaction: tx_id */
int TestDmlCommon::build_table_scan_param(
    const uint64_t tenant_id,
    ObTransID &tx_id,
    const share::schema::ObTableParam &table_param,
    ObTableScanParam &scan_param)
{
  int ret = build_table_scan_param_base_(tenant_id, table_param, true, scan_param);
  if (OB_SUCC(ret)) {
    scan_param.tx_id_ = tx_id;
  }
  return ret;
}

/* table_scan read_by_snapshot */
int TestDmlCommon::build_table_scan_param(
    const uint64_t tenant_id,
    ObTxReadSnapshot &read_snapshot,
    const share::schema::ObTableParam &table_param,
    ObTableScanParam &scan_param)
{
  int ret = build_table_scan_param_base_(tenant_id, table_param, false, scan_param);
  if (FAILEDx(scan_param.snapshot_.assign(read_snapshot))) {
    STORAGE_LOG(WARN, "assign snapshot fail", K(ret));
  }
  return ret;
}

int TestDmlCommon::build_table_scan_param_base_(
    const uint64_t tenant_id,
    const share::schema::ObTableParam &table_param,
    bool read_latest,
    ObTableScanParam &scan_param)
{
  int ret = OB_SUCCESS;

  int64_t expire_time = ObTimeUtility::current_time() + TX_EXPIRE_TIME_US;
  const uint64_t table_id = TEST_DATA_TABLE_ID;

  scan_param.column_ids_.push_back(OB_APP_MIN_COLUMN_ID + 0); // pk
  scan_param.column_ids_.push_back(OB_APP_MIN_COLUMN_ID + 1); // c1
  scan_param.column_ids_.push_back(OB_APP_MIN_COLUMN_ID + 2); // c2
  scan_param.column_ids_.push_back(OB_APP_MIN_COLUMN_ID + 3); // c3
  scan_param.column_ids_.push_back(OB_APP_MIN_COLUMN_ID + 4); // c4

  scan_param.tablet_id_ = TEST_DATA_TABLE_ID;

  scan_param.table_param_ = &table_param;
  scan_param.index_id_ = table_id; // table id
  scan_param.is_get_ = false;
  scan_param.timeout_ = expire_time;

  ObQueryFlag query_flag(ObQueryFlag::Forward, // scan_order
                         false, // daily_merge
                         false, // optimize
                         false, // sys scan
                         false, // full_row
                         false, // index_back
                         false, // query_stat
                         ObQueryFlag::MysqlMode, // sql_mode
                         read_latest // read_latest
                        );
  scan_param.scan_flag_.flag_ = query_flag.flag_;

  scan_param.reserved_cell_count_ = 5;
  scan_param.allocator_ = &CURRENT_CONTEXT->get_arena_allocator();
  scan_param.for_update_ = false;
  scan_param.for_update_wait_timeout_ = expire_time;
  scan_param.sql_mode_ = SMO_DEFAULT;
  scan_param.scan_allocator_ = &CURRENT_CONTEXT->get_arena_allocator();
  scan_param.frozen_version_ = -1;
  scan_param.force_refresh_lc_ = false;
  scan_param.output_exprs_ = nullptr;
  scan_param.aggregate_exprs_ = nullptr;
  scan_param.op_ = nullptr;
  scan_param.row2exprs_projector_ = nullptr;
  scan_param.schema_version_ = share::OB_CORE_SCHEMA_VERSION + 1;
  scan_param.tenant_schema_version_ = share::OB_CORE_SCHEMA_VERSION + 1;
  scan_param.limit_param_.limit_ = -1;
  scan_param.limit_param_.offset_ = 0;
  scan_param.need_scn_ = false;
  scan_param.pd_storage_flag_ = false;
  scan_param.fb_snapshot_.reset();

  ObNewRange range;
  range.table_id_ = table_id;
  range.start_key_.set_min_row();
  range.end_key_.set_max_row();
  scan_param.key_ranges_.push_back(range);

  return ret;
}

void TestDmlCommon::build_data_table_schema(
    const uint64_t tenant_id,
    share::schema::ObTableSchema &table_schema)
{
  const uint64_t table_id = TEST_DATA_TABLE_ID;
  const int64_t micro_block_size = 16 * 1024;

  table_schema.reset();
  table_schema.set_table_name("test_dml_common");
  table_schema.set_tablegroup_id(1);
  table_schema.set_database_id(1);
  table_schema.set_table_id(table_id);
  table_schema.set_schema_version(share::OB_CORE_SCHEMA_VERSION + 1);
  table_schema.set_rowkey_column_num(1);
  table_schema.set_max_used_column_id(ObObjType::ObExtendType - 1);
  table_schema.set_block_size(micro_block_size);
  table_schema.set_compress_func_name("none");
  table_schema.set_row_store_type(ObRowStoreType::ENCODING_ROW_STORE);
  table_schema.set_storage_format_version(ObStorageFormatVersion::OB_STORAGE_FORMAT_VERSION_V4);
  table_schema.set_micro_index_clustered(false);

#define TEST_DML_ADD_COLUMN(column_id, column_name, data_type, collation_type, is_row_key) \
  { \
    ObColumnSchemaV2 column; \
    column.set_column_id(column_id); \
    column.set_column_name(column_name); \
    column.set_data_type(data_type); \
    column.set_collation_type(collation_type); \
    if (is_row_key) { \
      column.set_rowkey_position(1); \
    } \
    table_schema.add_column(column); \
  }

  // table schema
  // a(bigint)  b(bigint)  c(bigint)  d(varchar)  e(varchar)
  TEST_DML_ADD_COLUMN(OB_APP_MIN_COLUMN_ID + 0, "a", ObIntType, CS_TYPE_UTF8MB4_GENERAL_CI, true);
  TEST_DML_ADD_COLUMN(OB_APP_MIN_COLUMN_ID + 1, "b", ObIntType, CS_TYPE_UTF8MB4_GENERAL_CI, false);
  TEST_DML_ADD_COLUMN(OB_APP_MIN_COLUMN_ID + 2, "c", ObIntType, CS_TYPE_UTF8MB4_GENERAL_CI, false);
  TEST_DML_ADD_COLUMN(OB_APP_MIN_COLUMN_ID + 3, "d", ObVarcharType, CS_TYPE_UTF8MB4_BIN, false);
  TEST_DML_ADD_COLUMN(OB_APP_MIN_COLUMN_ID + 4, "e", ObVarcharType, CS_TYPE_UTF8MB4_BIN, false);
#undef TEST_DML_ADD_COLUMN
}

void TestDmlCommon::build_index_table_schema(
    const uint64_t tenant_id,
    share::schema::ObTableSchema &table_schema)
{
  const uint64_t data_table_id = TEST_DATA_TABLE_ID;
  const uint64_t index_table_id = TEST_INDEX_TABLE_ID;
  const int64_t micro_block_size = 16 * 1024;

  table_schema.reset();
  table_schema.set_table_name("test_dml_common_index");
  table_schema.set_tablegroup_id(1);
  table_schema.set_database_id(1);
  table_schema.set_data_table_id(data_table_id);
  table_schema.set_table_id(index_table_id);
  table_schema.set_index_type(ObIndexType::INDEX_TYPE_NORMAL_LOCAL);
  table_schema.set_rowkey_column_num(1);
  table_schema.set_max_used_column_id(ObObjType::ObExtendType - 1);
  table_schema.set_block_size(micro_block_size);
  table_schema.set_row_store_type(ObRowStoreType::ENCODING_ROW_STORE);
  table_schema.set_storage_format_version(ObStorageFormatVersion::OB_STORAGE_FORMAT_VERSION_V4);

  // add index column: a
  {
    ObColumnSchemaV2 column;
    column.set_column_id(OB_APP_MIN_COLUMN_ID);
    column.set_column_name("index_a");
    column.set_data_type(ObIntType);
    column.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    column.set_rowkey_position(1);
    table_schema.add_column(column);
  }
}

int TestDmlCommon::build_tx_desc(const uint64_t tenant_id, ObTxDesc *&tx_desc)
{
  int ret = OB_SUCCESS;
  transaction::ObTransService *tx_service = MTL(transaction::ObTransService*);
  if (OB_FAIL(tx_service->acquire_tx(tx_desc, 100))) {
    STORAGE_LOG(WARN, "failed to acquire tx", K(ret));
  } else {
    STORAGE_LOG(INFO, "acquired tx desc", KPC(tx_desc));
  }
  return ret;
}

void TestDmlCommon::build_tx_param(ObTxParam &tx_param)
{
  tx_param.access_mode_ = transaction::ObTxAccessMode::RW;
  tx_param.isolation_ = transaction::ObTxIsolationLevel::RC;
  tx_param.timeout_us_ = TX_EXPIRE_TIME_US;
  STORAGE_LOG(INFO, "build tx param", K(tx_param));
}

void TestDmlCommon::release_tx_desc(ObTxDesc &tx_desc)
{
  transaction::ObTransService *tx_service = MTL(transaction::ObTransService*);
  tx_service->release_tx(tx_desc);
}

} // namespace storage
} // namespace oceanbase

#endif // OB_TEST_DML_COMMON_H_
