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
 #include "rootserver/ob_ai_model_ddl_service.h"
 #include "rootserver/ob_ai_model_ddl_operator.h"

namespace oceanbase
{
namespace rootserver
{

int ObAiModelDDLService::create_ai_model(const obrpc::ObCreateAiModelArg &arg)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = arg.exec_tenant_id_;
  ObSchemaGetterGuard schema_guard;
  ObAiModelSchema new_schema;
  const ObAiModelSchema *old_schema = nullptr;
  if (OB_FAIL(new_schema.assign(tenant_id, arg.model_info_))) {
    LOG_WARN("failed to assign new schema", K(ret), K(arg.model_info_));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
    LOG_WARN("fail to get schema guard with version in inner table", K(ret), K(tenant_id));
  } else if (OB_FAIL(schema_guard.get_ai_model_schema(tenant_id, arg.model_info_.get_name(), old_schema))) {
    LOG_WARN("failed to get ai model schema", K(ret), K(tenant_id), K(arg.model_info_.get_name()));
  } else if (OB_NOT_NULL(old_schema)) {
    ret = OB_AI_FUNC_MODEL_EXISTS;
    LOG_USER_ERROR(OB_AI_FUNC_MODEL_EXISTS, arg.model_info_.get_name().length(), arg.model_info_.get_name().ptr());
    LOG_WARN("ai model already exists", K(ret), K(tenant_id), K(arg.model_info_.get_name()));
  } else {
    ObDDLSQLTransaction trans(&ddl_service_.get_schema_service());
    ObAiModelDDLOperator ddl_operator(ddl_service_.get_schema_service());
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(tenant_id, refreshed_schema_version))) {
      LOG_WARN("failed to get tenant schema version", KR(ret), K(tenant_id));
    } else if (OB_FAIL(trans.start(&ddl_service_.get_sql_proxy(), tenant_id, refreshed_schema_version))) {
      LOG_WARN("start transaction failed", KR(ret), K(tenant_id), K(refreshed_schema_version));
    } else if (OB_FAIL(ddl_operator.create_ai_model(new_schema, arg.ddl_stmt_str_, trans))) {
      LOG_WARN("failed to create ai model", K(new_schema), K(ret));
    }

    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_WARN("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service_.publish_schema(tenant_id))) {
        LOG_WARN("publish schema failed", K(ret));
      }
    }
  }

  return ret;
}

int ObAiModelDDLService::drop_ai_model(const obrpc::ObDropAiModelArg &arg)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = arg.exec_tenant_id_;
  const ObString &ai_model_name = arg.get_ai_model_name();
  ObSchemaGetterGuard schema_guard;
  const ObAiModelSchema *old_schema = nullptr;

  if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
    LOG_WARN("fail to get schema guard with version in inner table", K(ret), K(tenant_id));
  } else if (OB_FAIL(schema_guard.get_ai_model_schema(tenant_id, ai_model_name, old_schema))) {
    LOG_WARN("failed to get ai model schema", K(ret), K(tenant_id), K(ai_model_name));
  } else if (OB_ISNULL(old_schema)) {
    ret = OB_AI_FUNC_MODEL_NOT_FOUND;
    LOG_USER_ERROR(OB_AI_FUNC_MODEL_NOT_FOUND, ai_model_name.length(), ai_model_name.ptr());
    LOG_WARN("ai model not found", K(ret), K(tenant_id), K(ai_model_name));
  } else {
    ObDDLSQLTransaction trans(&ddl_service_.get_schema_service());
    ObAiModelDDLOperator ddl_operator(ddl_service_.get_schema_service());
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(tenant_id, refreshed_schema_version))) {
      LOG_WARN("failed to get tenant schema version", KR(ret), K(tenant_id));
    } else if (OB_FAIL(trans.start(&ddl_service_.get_sql_proxy(), tenant_id, refreshed_schema_version))) {
      LOG_WARN("start transaction failed", KR(ret), K(tenant_id), K(refreshed_schema_version));
    } else if (OB_FAIL(ddl_operator.drop_ai_model(*old_schema, arg.ddl_stmt_str_, trans))) {
      LOG_WARN("failed to drop ai model", KPC(old_schema), K(ret));
    }

    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_WARN("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service_.publish_schema(tenant_id))) {
        LOG_WARN("publish schema failed", K(ret));
      }
    }
  }
  return ret;
}

} // end namespace rootserver
} // end namespace oceanbase
