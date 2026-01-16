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
#ifndef OB_ROOTSERVER_TRUNCATE_INFO_TRUNCATE_INFO_SERVICE_H_
#define OB_ROOTSERVER_TRUNCATE_INFO_TRUNCATE_INFO_SERVICE_H_
#include "/usr/include/stdint.h"
#include "rootserver/truncate_info/ob_truncate_tablet_arg.h"
#include "storage/truncate_info/ob_truncate_partition_filter.h"
#include "sql/resolver/expr/ob_raw_expr.h"
namespace oceanbase
{
namespace obrpc
{
struct ObAlterTableArg;
}
namespace share
{
namespace schema
{
class ObTableSchema;
}
}
namespace common
{
class ObMySQLTransaction;
}
namespace observer
{
class ObInnerSQLConnection;
}
namespace rootserver
{
class ObDDLOperator;

struct ObTruncatePartKeyInfo final
{
  ObTruncatePartKeyInfo(ObIAllocator &allocator);
  ~ObTruncatePartKeyInfo();
  int init(
    ObIAllocator &allocator,
    const uint64_t tenant_id,
    const ObTableSchema &data_table_schema);
  bool is_valid() const
  {
    return nullptr != part_expr_ && !ref_column_ids_.empty();
  }
  int check_only_have_ref_columns(
    const share::schema::ObTableSchema &data_table_schema,
    const ObPartitionLevel check_part_level,
    bool &only_ref_columns);
  int check_stored_ref_columns_for_index(
    const share::schema::ObTableSchema &index_table_schema,
    bool &stored_ref_columns);
  int get_ref_column_id_array(
    const ObTableSchema &index_table_schema,
    const ObPartitionLevel part_level,
    ObIArray<int64_t> &ref_column_idx_array);
  TO_STRING_KV(KPC_(part_expr), KPC_(subpart_expr), K_(ref_column_ids));
private:
  int create_tmp_session(
      const uint64_t tenant_id,
      const bool is_oracle_mode,
      share::schema::ObSchemaGetterGuard &schema_guard,
      sql::ObFreeSessionCtx &free_session_ctx,
      sql::ObSQLSessionInfo *&session);
  void release_tmp_session(
      sql::ObFreeSessionCtx &free_session_ctx,
      sql::ObSQLSessionInfo *&session);
  int resolve_part_expr(
      ObIAllocator &allocator,
      share::schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema &table_schema,
      const ObPartitionLevel part_level,
      sql::ObRawExpr *&raw_part_expr);
  int extract_col_ref_expr(
    sql::ObRawExpr *part_expr,
    common::ObIArray<sql::ObRawExpr*> &exprs);
  int inner_check_only_have_ref_columns(
    const share::schema::ObTableSchema &data_table_schema,
    const ObPartitionLevel part_level,
    bool &only_ref_columns);
  int inner_check_column_schema(
    const ObTableSchema &data_table_schema,
    sql::ObRawExpr &expr,
    bool &only_ref_columns);
private:
  static const int64_t REF_COLUMN_ID_CNT = 4;
  sql::ObSQLSessionInfo *session_;
  sql::ObFreeSessionCtx free_session_ctx_;
  sql::ObRawExprFactory expr_factory_;
  sql::ObRawExpr *part_expr_;
  sql::ObRawExpr *subpart_expr_;
  ObSEArray<uint64_t, REF_COLUMN_ID_CNT> ref_column_ids_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTruncatePartKeyInfo);
};

struct ObTruncateInfoService final
{
public:
  ObTruncateInfoService(
    const obrpc::ObAlterTableArg &arg,
    const share::schema::ObTableSchema &data_table_schema);
  int init(ObMySQLProxy &sql_proxy);
  int check_only_have_ref_columns(
    const obrpc::ObAlterTableArg::AlterPartitionType &alter_type,
    bool &only_ref_columns);
  int check_stored_ref_columns_for_index(
    const share::schema::ObTableSchema &index_table_schema,
    bool &stored_ref_columns);
  int execute(
    common::ObMySQLTransaction &trans,
    ObDDLOperator &ddl_operator,
    share::schema::ObTableSchema &index_table_schema);
private:
  uint64_t get_tenant_id() const;
  static int gen_new_schema_version_for_index_(
    ObMySQLTransaction &trans,
    ObDDLOperator &ddl_operator,
    share::schema::ObTableSchema &index_table_schema);
  int loop_part_to_register_mds_(
    observer::ObInnerSQLConnection &conn,
    const share::schema::ObTableSchema &index_table_schema);
  int loop_subpart_to_register_mds_(
    observer::ObInnerSQLConnection &conn,
    const share::schema::ObTableSchema &index_table_schema);
  int loop_index_tablet_id_to_register_(
    observer::ObInnerSQLConnection &conn,
    ObTruncateTabletArg &truncate_arg);
  int register_mds_(
    observer::ObInnerSQLConnection &conn,
    const ObTruncateTabletArg &arg);
  int retry_register_mds_(
    observer::ObInnerSQLConnection &conn,
    const ObTruncateTabletArg &arg,
    const char *buf,
    const int64_t buf_len);
  static bool need_retry_errno(const int ret);
  static const int64_t SLEEP_INTERVAL = 100 * 1000L; // 100ms
private:
  ObArenaAllocator allocator_; // for part_key_info_, only init once
  ObArenaAllocator loop_allocator_; // for loop index tablets
  const obrpc::ObAlterTableArg &arg_;
  const share::schema::ObTableSchema &data_table_schema_;
  ObSEArray<ObTabletID, 8> index_tablet_array_;
  ObSEArray<share::ObLSID, 8> ls_id_array_;
  ObTruncatePartKeyInfo part_key_info_;
  int64_t ddl_task_id_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTruncateInfoService);
};

} // namespace rootserver
} // namespace oceanbase

#endif // OB_ROOTSERVER_TRUNCATE_INFO_TRUNCATE_INFO_SERVICE_H_
