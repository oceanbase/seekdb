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
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
namespace rootserver
{

static int check_tenant_not_active()
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObSimpleTenantSchema *tenant_schema = nullptr;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("get tenant schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_schema))) {
    LOG_WARN("get tenant schema failed", K(ret));
  } else if (OB_ISNULL(tenant_schema)) {
    ret = OB_TENANT_NOT_EXIST;
    LOG_WARN("tenant does not exist", K(ret));
  } else if (tenant_schema->is_normal() || tenant_schema->is_dropping()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tenant is still active", K(ret));
  } else {
    LOG_INFO("tenant is not active", K(ret));
  }
  return ret;
}

// Notice: this function is only used for dropping lob aux table that's main table has been dropped casued by some bugs.
int ObDDLService::force_drop_lonely_lob_aux_table(const obcall::ObForceDropLonelyLobAuxTableArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid drop lob arg", KR(ret), K(arg));
  } else if (OB_ISNULL(schema_service_) || OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service_ or sql_proxy_ is null", KR(ret), KP(schema_service_), KP(sql_proxy_));
  } else {
    ObSchemaGetterGuard schema_guard;
    ObDDLSQLTransaction trans(schema_service_);
    ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
    int64_t refreshed_schema_version = 0;
    const ObTableSchema *lob_meta_table_schema_ptr = nullptr;
    const ObTableSchema *lob_piece_table_schema_ptr = nullptr;
    
    uint64_t data_table_id = arg.get_data_table_id();
    bool exist = false;
    bool ignore_ls_not_exist_for_lob_meta = false;
    bool ignore_ls_not_exist_for_lob_piece = false;

    HEAP_VAR(ObTableSchema, tmp_lob_table_schema) {
      if (OB_FAIL(get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
        LOG_WARN("fail to get schema guard with version in inner table", KR(ret));
      } else if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
        LOG_WARN("fail to get tenant schema version", KR(ret));
      } else if (OB_FAIL(trans.start(sql_proxy_, refreshed_schema_version))) {
        LOG_WARN("fail to start trans", KR(ret), K(refreshed_schema_version));

      // 1. check data table exist. if exist, it's not allowed to drop
      } else if (OB_FAIL(schema_guard.check_table_exist(data_table_id, exist))) {
        LOG_WARN("fail to check table exist", KR(ret), K(arg));
      } else if (exist) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("data table exist, so cannot drop lob table", KR(ret), K(arg));

      // 2. get and check lob meta table
      } else if (OB_FAIL(check_and_get_aux_table_schema(schema_guard, arg.get_aux_lob_meta_table_id(),
          data_table_id, ObTableType::AUX_LOB_META, lob_meta_table_schema_ptr))) {
        LOG_WARN("fail to get lob meta table schema", KR(ret), K(arg));
      // 3. get and check lob piece table
      } else if (OB_FAIL(check_and_get_aux_table_schema(schema_guard, arg.get_aux_lob_piece_table_id(),
          data_table_id, ObTableType::AUX_LOB_PIECE, lob_piece_table_schema_ptr))) {
        LOG_WARN("fail to get lob piece table schema", KR(ret), K(arg));

      // 4. drop lob meta table
      } else if (OB_FAIL(tmp_lob_table_schema.assign(*lob_meta_table_schema_ptr))) {
        LOG_WARN("fail to assign lob meta table schema", KR(ret));
      } else if (OB_FAIL(ddl_operator.drop_table(tmp_lob_table_schema, trans, nullptr/*ddl_stmt_str*/, false/*is_truncate_table*/,
          nullptr/*drop_table_set*/, false/*is_drop_db*/, true/*delete_priv*/, true/*is_force_drop_lonely_lob_aux_table*/))) {
        LOG_ERROR("fail to drop lob meta table", KR(ret), K(tmp_lob_table_schema));
        if (OB_LS_NOT_EXIST == ret || OB_LS_IS_DELETED == ret) {
          int tmp_ret = OB_SUCCESS;
          if (OB_TMP_FAIL(check_tenant_not_active())) {
            LOG_WARN("check tenant state failed", KR(tmp_ret));
          } else {
            LOG_ERROR("ls not exist, ignore this when drop lob meta aux table", KR(ret), K(tmp_lob_table_schema));
            ret = OB_SUCCESS;
            ignore_ls_not_exist_for_lob_meta = true;
          }
        }
      }
    
      // 5. drop lob piece table
      if (OB_FAIL(ret)) {
      } else if (FALSE_IT(tmp_lob_table_schema.reset())) {
      } else if (OB_FAIL(tmp_lob_table_schema.assign(*lob_piece_table_schema_ptr))) {
        LOG_WARN("fail to assign lob piece table schema", KR(ret));
      } else if (OB_FAIL(ddl_operator.drop_table(tmp_lob_table_schema, trans, nullptr/*ddl_stmt_str*/, false/*is_truncate_table*/,
          nullptr/*drop_table_set*/, false/*is_drop_db*/, true/*delete_priv*/, true/*is_force_drop_lonely_lob_aux_table*/))) {
        LOG_WARN("fail to drop lob piece table", KR(ret), K(tmp_lob_table_schema));
        if (OB_LS_NOT_EXIST == ret || OB_LS_IS_DELETED == ret) {
          int tmp_ret = OB_SUCCESS;
          if (OB_TMP_FAIL(check_tenant_not_active())) {
             LOG_WARN("check tenant state failed", KR(tmp_ret));
          } else {
            LOG_ERROR("ls not exist, ignore this when drop lob piece aux table", KR(ret), K(tmp_lob_table_schema));
            ret = OB_SUCCESS;
            ignore_ls_not_exist_for_lob_piece = true;
          }
        }
      }

      if (OB_SUCC(ret)) {
        if (ignore_ls_not_exist_for_lob_meta && ! ignore_ls_not_exist_for_lob_piece) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("ignore_ls_not_exist_for_lob_meta but not ignore_ls_not_exist_for_lob_piece, unexpected situation", KR(ret), K(arg));
        } else if (! ignore_ls_not_exist_for_lob_meta && ignore_ls_not_exist_for_lob_piece) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("not ignore_ls_not_exist_for_lob_meta but ignore_ls_not_exist_for_lob_piece, unexpected situation", KR(ret), K(arg));
        }
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
      LOG_WARN("publish_schema failed", KR(ret));
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
    LOG_WARN("failed get_table_schema", KR(ret), K(aux_table_id), K(data_table_id), K(table_type));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("aux table schema is null", KR(ret), K(aux_table_id), K(data_table_id), K(table_type));
  } else if (table_schema->get_data_table_id() != data_table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("data table id is not match", KR(ret), K(data_table_id), K(table_type),
        K(table_schema->get_data_table_id()), KPC(table_schema));
  } else if (table_schema->get_table_type() != table_type) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table is not expected", KR(ret), K(aux_table_id), K(data_table_id), K(table_type),
        K(table_schema->get_table_type()), KPC(table_schema));
  }
  return ret;
}

} // end namespace rootserver
} // end namespace oceanbase
