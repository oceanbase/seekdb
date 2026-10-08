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


#include "ob_ddl_service.h"

namespace oceanbase
{
namespace rootserver
{

// Notice: this function is only used for dropping lob aux table that's main table has been dropped casued by some bugs.
int ObDDLService::force_drop_lonely_lob_aux_table(const obcall::ObForceDropLonelyLobAuxTableArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(schema_service_) || OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObSchemaGetterGuard schema_guard;
    ObDDLSQLTransaction trans(schema_service_);
    ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
    int64_t refreshed_schema_version = 0;
    const ObTableSchema *lob_meta_table_schema_ptr = nullptr;
    const ObTableSchema *lob_piece_table_schema_ptr = nullptr;
    
    uint64_t data_table_id = arg.get_data_table_id();
    bool exist = false;

    HEAP_VAR(ObTableSchema, tmp_lob_table_schema) {
      if (OB_FAIL(get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
      } else if (OB_FAIL(trans.start(sql_proxy_, refreshed_schema_version))) {
      } else if (OB_FAIL(schema_guard.check_table_exist(data_table_id, exist))) {
      } else if (exist) {
        ret = OB_INVALID_ARGUMENT;

      // 2. get and check lob meta table
      } else if (OB_FAIL(check_and_get_aux_table_schema(schema_guard, arg.get_aux_lob_meta_table_id(),
          data_table_id, ObTableType::AUX_LOB_META, lob_meta_table_schema_ptr))) {
      } else if (OB_FAIL(check_and_get_aux_table_schema(schema_guard, arg.get_aux_lob_piece_table_id(),
          data_table_id, ObTableType::AUX_LOB_PIECE, lob_piece_table_schema_ptr))) {
      } else if (OB_FAIL(tmp_lob_table_schema.assign(*lob_meta_table_schema_ptr))) {
      } else if (OB_FAIL(ddl_operator.drop_table(tmp_lob_table_schema, trans, nullptr/*ddl_stmt_str*/, false/*is_truncate_table*/,
          nullptr/*drop_table_set*/, false/*is_drop_db*/, true/*delete_priv*/, true/*is_force_drop_lonely_lob_aux_table*/))) {
      }
    
      // 5. drop lob piece table
      if (OB_FAIL(ret)) {
      } else if (FALSE_IT(tmp_lob_table_schema.reset())) {
      } else if (OB_FAIL(tmp_lob_table_schema.assign(*lob_piece_table_schema_ptr))) {
      } else if (OB_FAIL(ddl_operator.drop_table(tmp_lob_table_schema, trans, nullptr/*ddl_stmt_str*/, false/*is_truncate_table*/,
          nullptr/*drop_table_set*/, false/*is_drop_db*/, true/*delete_priv*/, true/*is_force_drop_lonely_lob_aux_table*/))) {
      }
    }

    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR_RET(temp_ret, "trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(publish_schema())) {
    }
  }
  LOG_ERROR("NOTICE: there are force_drop_lonely_lob_aux_table", KR(ret), K(arg));
  return ret;
}

int ObDDLService::check_and_get_aux_table_schema(ObSchemaGetterGuard &schema_guard, const uint64_t aux_table_id,
                                                 const uint64_t data_table_id, const ObTableType table_type, const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(schema_guard.get_table_schema( aux_table_id, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (table_schema->get_data_table_id() != data_table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("data table id is not match", KR(ret), K(data_table_id), K(table_type),
        K(table_schema->get_data_table_id()), KPC(table_schema));
  } else if (table_schema->get_table_type() != table_type) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

} // end namespace rootserver
} // end namespace oceanbase
