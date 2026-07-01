#include "rootserver/ob_root_service.h"
#include "rootserver/ob_rs_serial_call.h"
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

#define USING_LOG_PREFIX PL

#include "ob_dbms_ai_service.h"
#include "share/ai_service/ob_ai_service_executor.h"
#include "share/ai_service/ob_ai_service_struct.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "share/ob_rpc_struct.h"
#include "src/pl/ob_pl.h"
#include "sql/privilege_check/ob_ai_model_priv_util.h"

using namespace oceanbase::share;
using namespace oceanbase::obcall;

namespace oceanbase
{
namespace pl
{

int ObDBMSAiService::check_ai_model_privilege_(ObPLExecCtx &ctx, ObPrivSet required_priv)
{
  int ret = OB_SUCCESS;
  bool has_priv = false;
  
  if (OB_ISNULL(ctx.exec_ctx_) || OB_ISNULL(ctx.exec_ctx_->get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("exec_ctx or session is null", K(ret));
  } else {
    ObArenaAllocator tmp_allocator;
    share::schema::ObSchemaGetterGuard *schema_guard = ctx.exec_ctx_->get_sql_ctx()->schema_guard_;
    if (OB_ISNULL(schema_guard)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema guard is null", K(ret));
    } else {
      sql::ObAIServiceEndpointPrivUtil priv_util(*schema_guard);
      share::schema::ObSessionPrivInfo session_priv;
      if (OB_FAIL(schema_guard->get_session_priv_info(
                                                    ctx.exec_ctx_->get_my_session()->get_priv_user_id(),
                                                    ctx.exec_ctx_->get_my_session()->get_database_name(),
                                                    session_priv))) {
      } else {
        switch (required_priv) {
          case OB_PRIV_CREATE_AI_MODEL:
            if (OB_FAIL(priv_util.check_create_ai_model_priv(tmp_allocator, session_priv, has_priv))) {
            }
            break;
          case OB_PRIV_ALTER_AI_MODEL:
            if (OB_FAIL(priv_util.check_alter_ai_model_priv(tmp_allocator, session_priv, has_priv))) {
            }
            break;
          case OB_PRIV_DROP_AI_MODEL:
            if (OB_FAIL(priv_util.check_drop_ai_model_priv(tmp_allocator, session_priv, has_priv))) {
            }
            break;
          case OB_PRIV_ACCESS_AI_MODEL:
            if (OB_FAIL(priv_util.check_access_ai_model_priv(tmp_allocator, session_priv, has_priv))) {
            }
            break;
          default:
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid privilege type", K(ret), K(required_priv));
            break;
        }
        
        if (OB_SUCC(ret) && !has_priv) {
          ret = OB_ERR_NO_PRIVILEGE;
          LOG_WARN("no privilege for ai model operation", K(ret), K(required_priv));
        }
      }
    }
  }
  
  return ret;
}

int ObDBMSAiService::create_ai_model_endpoint(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString endpoint_name;
  ctx.set_is_sensitive(true);

  if (OB_FAIL(precheck_version_and_param_count_(2, params))) {
  } else if (OB_FAIL(ObDBMSAiService::check_ai_model_privilege_(ctx, OB_PRIV_CREATE_AI_MODEL))) {
    if (OB_ERR_NO_PRIVILEGE == ret) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("failed to check create ai model privilege", K(ret));
      LOG_USER_ERROR(OB_ERR_NO_PRIVILEGE, "create ai model endpoint");
    } else {
      LOG_WARN("failed to check create ai model privilege", K(ret));
    }
  } else if (OB_FAIL(params.at(0).get_string(endpoint_name))) {
  } else if (endpoint_name.empty()) {
    ret = OB_AI_FUNC_PARAM_EMPTY;
    LOG_WARN("ai service endpoint name is empty", K(ret), K(params));
    ObString var_name = "name";
    LOG_USER_ERROR(OB_AI_FUNC_PARAM_EMPTY, var_name.length(), var_name.ptr());
  } else if (params.at(1).is_null()) {
    ret = OB_AI_FUNC_PARAM_EMPTY;
    LOG_WARN("ai service endpoint params is wrong", K(ret), K(params));
    ObString var_name = "PARAMS";
    LOG_USER_ERROR(OB_AI_FUNC_PARAM_EMPTY, var_name.length(), var_name.ptr());
  } else {
    ObArenaAllocator tmp_allocator;
    ObIJsonBase *j_base = nullptr;
    if (OB_FAIL(get_json_base_(tmp_allocator, params, j_base))) {
    } else if (OB_FAIL(ObAiServiceExecutor::create_ai_model_endpoint(tmp_allocator, endpoint_name, *j_base))) {
    }
  }

  LOG_DEBUG("finished to create ai service endpoint", K(ret), K(params));
  return ret;
}

int ObDBMSAiService::alter_ai_model_endpoint(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString endpoint_name;

  if (OB_FAIL(precheck_version_and_param_count_(2, params))) {
  } else if (OB_FAIL(ObDBMSAiService::check_ai_model_privilege_(ctx, OB_PRIV_ALTER_AI_MODEL))) {
    if (OB_ERR_NO_PRIVILEGE == ret) {
      LOG_WARN("failed to check alter ai model privilege", K(ret));
      LOG_USER_ERROR(OB_ERR_NO_PRIVILEGE, "alter ai model endpoint");
    } else {
      LOG_WARN("failed to check alter ai model privilege", K(ret));
    }
  } else if (OB_FAIL(params.at(0).get_string(endpoint_name))) {
  } else if (endpoint_name.empty()) {
    ret = OB_AI_FUNC_PARAM_EMPTY;
    LOG_WARN("ai service endpoint name is empty", K(ret), K(params), K(endpoint_name));
    ObString var_name = "name";
    LOG_USER_ERROR(OB_AI_FUNC_PARAM_EMPTY, var_name.length(), var_name.ptr());
  } else if (params.at(1).is_null()) {
    ret = OB_AI_FUNC_PARAM_EMPTY;
    LOG_WARN("ai service endpoint params is wrong", K(ret), K(params));
    ObString var_name = "PARAMS";
    LOG_USER_ERROR(OB_AI_FUNC_PARAM_EMPTY, var_name.length(), var_name.ptr());
  } else {
    ObIJsonBase *j_base = nullptr;
    ObArenaAllocator tmp_allocator;
    if (OB_FAIL(get_json_base_(tmp_allocator, params, j_base))) {
    } else if (OB_FAIL(ObAiServiceExecutor::alter_ai_model_endpoint(tmp_allocator, endpoint_name, *j_base))) {
    }
  }

  LOG_DEBUG("finished to alter ai service endpoint", K(ret), K(params));
  return ret;  
}

int ObDBMSAiService::drop_ai_model_endpoint(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString endpoint_name;

  if (OB_FAIL(precheck_version_and_param_count_(1, params))) {
  } else if (OB_FAIL(ObDBMSAiService::check_ai_model_privilege_(ctx, OB_PRIV_DROP_AI_MODEL))) {
    if (OB_ERR_NO_PRIVILEGE == ret) {
      LOG_WARN("failed to check drop ai model privilege", K(ret));
      LOG_USER_ERROR(OB_ERR_NO_PRIVILEGE, "drop ai model endpoint");
    } else {
      LOG_WARN("failed to check drop ai model privilege", K(ret));
    }
  } else if (OB_FAIL(params.at(0).get_string(endpoint_name))) {
  } else if (endpoint_name.empty()) {
    ret = OB_AI_FUNC_PARAM_EMPTY;
    LOG_WARN("ai service endpoint name is empty", K(ret), K(params), K(endpoint_name));
    ObString var_name = "name";
    LOG_USER_ERROR(OB_AI_FUNC_PARAM_EMPTY, var_name.length(), var_name.ptr());
  } else if (OB_FAIL(ObAiServiceExecutor::drop_ai_model_endpoint(endpoint_name))) {
  }

  LOG_DEBUG("finished to drop ai service endpoint", K(ret), K(endpoint_name));

  return ret;  
}

int ObDBMSAiService::precheck_version_and_param_count_(int expect_param_count, sql::ParamStore &params)
{
  int ret = OB_SUCCESS;
  
  if (expect_param_count != params.count()) {
    ret = OB_INVALID_ARGUMENT_NUM;
    LOG_WARN("invalid argument", K(ret), K(params.count()));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT_NUM);
  }
  return ret;
}

int ObDBMSAiService::get_json_base_(ObArenaAllocator &allocator, sql::ParamStore &params, ObIJsonBase *&j_base)
{
  int ret = OB_SUCCESS;
  ObString j_str;
  ObJsonInType in_type = ObJsonInType::JSON_BIN;
  uint32_t parse_flag = 0; // mysql mode 

  if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(&allocator, params.at(1), j_str))) {
  } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator, j_str, in_type, in_type, j_base, parse_flag))) {
  } else if (j_base->json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ai service endpoint params is not a json object", K(ret), K(params));
    LOG_USER_ERROR(OB_AI_FUNC_PARAM_TYPE_INVALID, (int)strlen("PARAMS"), "PARAMS", (int)strlen("JSON_OBJECT"), "JSON_OBJECT");
  }
  return ret;
}

int ObDBMSAiService::create_ai_model(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString model_name;
  
  ObSchemaGetterGuard schema_guard;
  const ObAiModelSchema *ai_model_schema = nullptr;

  if (OB_FAIL(precheck_version_and_param_count_(2, params))) {
  } else if (OB_FAIL(ObDBMSAiService::check_ai_model_privilege_(ctx, OB_PRIV_CREATE_AI_MODEL))) {
    if (OB_ERR_NO_PRIVILEGE == ret) {
      LOG_WARN("failed to check create ai model privilege", K(ret));
      LOG_USER_ERROR(OB_ERR_NO_PRIVILEGE, "create ai model");
    } else {
      LOG_WARN("failed to check create ai model privilege", K(ret));
    }
  } else if (OB_FAIL(params.at(0).get_string(model_name))) {
  } else if (model_name.empty()) {
    ret = OB_AI_FUNC_PARAM_EMPTY;
    LOG_WARN("ai model name is empty", K(ret), K(params), K(model_name));
    ObString var_name = "name";
    LOG_USER_ERROR(OB_AI_FUNC_PARAM_EMPTY, var_name.length(), var_name.ptr());
  } else if (params.at(1).is_null()) {
    ret = OB_AI_FUNC_PARAM_EMPTY;
    LOG_WARN("ai model params is null", K(ret), K(params));
    ObString var_name = "PARAMS";
    LOG_USER_ERROR(OB_AI_FUNC_PARAM_EMPTY, var_name.length(), var_name.ptr());
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_ai_model_schema( model_name, ai_model_schema))) {
  } else if (OB_NOT_NULL(ai_model_schema)) {
    ret = OB_AI_FUNC_MODEL_EXISTS;
    LOG_WARN("ai model already exists", K(ret), K(model_name));
    LOG_USER_ERROR(OB_AI_FUNC_MODEL_EXISTS, model_name.length(), model_name.ptr());
  } else if (OB_ISNULL(ctx.exec_ctx_)) {
    ret =  OB_ERR_UNEXPECTED;
    LOG_WARN("exec context is null", K(ret));
  } else if (OB_ISNULL(ctx.exec_ctx_->get_sql_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else {
    ObArenaAllocator tmp_allocator;
    ObIJsonBase *j_base = nullptr;
    ObAiServiceModelInfo model_info;
    if (OB_FAIL(get_json_base_(tmp_allocator, params, j_base))) {
    } else if (OB_FAIL(model_info.parse_from_json_base(model_name, *j_base))) {
    } else {
      ObCreateAiModelArg arg(model_info);
      arg.ddl_stmt_str_ = ctx.exec_ctx_->get_sql_ctx()->cur_sql_;
      if (OB_FAIL(arg.check_valid())) {
      } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->create_ai_model(arg); }))) {
      }
    }

    LOG_DEBUG("finished to create ai model", K(ret), K(params), K(model_name));
  }
  return ret;
}

int ObDBMSAiService::drop_ai_model(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result)
{
  int ret = OB_SUCCESS;
  ObString model_name;
  
  ObSchemaGetterGuard schema_guard;
  const ObAiModelSchema *ai_model_schema = nullptr;

  if (OB_FAIL(precheck_version_and_param_count_(1, params))) {
  } else if (OB_FAIL(ObDBMSAiService::check_ai_model_privilege_(ctx, OB_PRIV_DROP_AI_MODEL))) {
    if (OB_ERR_NO_PRIVILEGE == ret) {
      LOG_WARN("failed to check drop ai model privilege", K(ret));
      LOG_USER_ERROR(OB_ERR_NO_PRIVILEGE, "drop ai model");
    } else {
      LOG_WARN("failed to check drop ai model privilege", K(ret));
    }
  } else if (OB_FAIL(params.at(0).get_string(model_name))) {
  } else if (model_name.empty()) {
    ret = OB_AI_FUNC_PARAM_EMPTY;
    LOG_WARN("ai model name is empty", K(ret), K(params), K(model_name));
    ObString var_name = "name";
    LOG_USER_ERROR(OB_AI_FUNC_PARAM_EMPTY, var_name.length(), var_name.ptr());
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_ai_model_schema( model_name, ai_model_schema))) {
  } else if (OB_ISNULL(ai_model_schema)) {
    ret = OB_AI_FUNC_MODEL_NOT_FOUND;
    LOG_WARN("ai model not exists", K(ret), K(model_name));
    LOG_USER_ERROR(OB_AI_FUNC_MODEL_NOT_FOUND, model_name.length(), model_name.ptr());
  } else if (OB_ISNULL(ctx.exec_ctx_)) {
    ret =  OB_ERR_UNEXPECTED;
    LOG_WARN("exec context is null", K(ret));
  } else if (OB_ISNULL(ctx.exec_ctx_->get_sql_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else {
    ObDropAiModelArg arg(model_name);
    arg.ddl_stmt_str_ = ctx.exec_ctx_->get_sql_ctx()->cur_sql_;
      if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->drop_ai_model(arg); }))) {
    }

    LOG_INFO("finished to drop ai model", K(ret), K(params), K(model_name));
  }

  return ret;
}

} // namespace pl
} // namespace oceanbase
