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
#include "rootserver/ob_location_ddl_service.h"
#include "rootserver/ob_location_ddl_operator.h"
#include "rootserver/ob_ddl_sql_generator.h"
#include "sql/ob_sql_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace obrpc;
namespace rootserver
{
int ObLocationDDLService::create_location(const obrpc::ObCreateLocationArg &arg,
                                          const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  const bool is_or_replace = arg.or_replace_;
  const uint64_t tenant_id = arg.schema_.get_tenant_id();
  const uint64_t user_id = arg.user_id_;
  const ObString &location_name = arg.schema_.get_location_name();
  const ObString &location_url = arg.schema_.get_location_url();
  const ObString &location_access_info = arg.schema_.get_location_access_info();
  const ObLocationSchema *schema_ptr = NULL;
  bool is_exist = false;
  ObLocationSchema new_schema;
  ObSchemaGetterGuard schema_guard;
  int64_t refreshed_schema_version = 0;
  uint64_t loc_id = OB_INVALID_ID;
  LOG_INFO("create location ddl service", K(ret), K(location_url), K(location_access_info));
  if (OB_ISNULL(ddl_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid input schema", K(ret), K(tenant_id));
  } else if (OB_FAIL(ddl_service_->get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
    LOG_WARN("failed to get schema guard with version in inner table", K(ret), K(tenant_id));
  } else if (OB_FAIL(schema_guard.get_schema_version(tenant_id, refreshed_schema_version))) {
    LOG_WARN("failed to get tenant schema version", KR(ret), K(tenant_id));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(tenant_id, location_name, schema_ptr))) {
    LOG_WARN("failed to get location schema by name", K(ret), K(tenant_id), K(location_name));
  } else if (NULL != schema_ptr) {
    is_exist = true;
    loc_id = schema_ptr->get_location_id();
    if (OB_FAIL(new_schema.assign(*schema_ptr))) {
      LOG_WARN("failed to assign new location schema", K(ret), K(*schema_ptr));
    } else if (OB_FAIL(new_schema.set_location_url(location_url))) {
      LOG_WARN("failed to set location path", K(ret), K(location_url));
    } else if (OB_FAIL(new_schema.set_location_access_info(location_access_info))) {
      LOG_WARN("failed to set location access id", K(ret), K(location_access_info));
    }
  } else if (NULL == schema_ptr) {
    if (OB_FAIL(new_schema.assign(arg.schema_))) {
      LOG_WARN("failed to assign new location schema", K(ret), K(arg));
    }
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (is_exist && !is_or_replace) {
    ret = OB_LOCATION_OBJ_EXIST;
    LOG_WARN("location already exists and is not replace operation", K(ret),
        K(is_or_replace), K(location_name));
  } else {
    ObDDLSQLTransaction trans(&ddl_service_->get_schema_service());
    ObLocationDDLOperator ddl_operator(ddl_service_->get_schema_service(), ddl_service_->get_sql_proxy());
    if (OB_FAIL(trans.start(&ddl_service_->get_sql_proxy(), tenant_id, refreshed_schema_version))) {
      LOG_WARN("failed to start transaction", KR(ret), K(tenant_id), K(refreshed_schema_version));
    } else if (is_exist && is_or_replace
        && OB_FAIL(ddl_operator.alter_location(*ddl_stmt_str, new_schema, trans))) {
      LOG_WARN("failed to alter location", K(ret), K(new_schema));
    } else if (!is_exist && OB_FAIL(ddl_operator.create_location(*ddl_stmt_str, user_id, new_schema, trans))) {
      LOG_WARN("failed to create location", K(ret), K(new_schema));
    }

    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_WARN("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ddl_service_->publish_schema(tenant_id))) {
      LOG_WARN("publish schema failed", K(ret));
    }
  }
  return ret;
}

int ObLocationDDLService::drop_location(const obrpc::ObDropLocationArg &arg,
                                        const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = arg.tenant_id_;
  const ObString &location_name = arg.location_name_;
  const ObLocationSchema *schema_ptr = NULL;
  bool is_exist = false;
  bool is_oracle_mode = false;
  ObSchemaGetterGuard schema_guard;
  int64_t refreshed_schema_version = 0;
  if (OB_ISNULL(ddl_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid input schema", K(ret), K(tenant_id));
  } else if (OB_FAIL(ddl_service_->get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
    LOG_WARN("failed to get schema guard with version in inner table", K(ret), K(tenant_id));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(tenant_id, location_name, schema_ptr))) {
    LOG_WARN("failed to get schema by location name", K(ret), K(tenant_id), K(location_name));
  } else if (OB_FAIL(schema_guard.get_schema_version(tenant_id, refreshed_schema_version))) {
    LOG_WARN("failed to get tenant schema version", KR(ret), K(tenant_id));
  } else if (NULL != schema_ptr) {
    is_exist = true;
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (!is_exist) {
    ret = OB_LOCATION_OBJ_NOT_EXIST;
    LOG_WARN("location does not exist", K(ret), K(location_name));
    LOG_USER_ERROR(OB_LOCATION_OBJ_NOT_EXIST,
                   static_cast<int>(location_name.length()),
                   location_name.ptr());
  } else {
    ObDDLSQLTransaction trans(&ddl_service_->get_schema_service());
    ObLocationDDLOperator ddl_operator(ddl_service_->get_schema_service(), ddl_service_->get_sql_proxy());
    ObLocationSchema schema;
    if (OB_FAIL(schema.assign(*schema_ptr))) {
      LOG_WARN("fail to assign location schema", K(ret), K(*schema_ptr));
    } else if (OB_FAIL(trans.start(&ddl_service_->get_sql_proxy(), tenant_id, refreshed_schema_version))) {
      LOG_WARN("failed to start transaction", KR(ret), K(tenant_id), K(refreshed_schema_version));
    } else if (OB_FAIL(ddl_operator.drop_location(*ddl_stmt_str, schema, trans))) {
      LOG_WARN("failed to drop location", K(ret), K(schema));
    }

    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_WARN("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ddl_service_->publish_schema(tenant_id))) {
      LOG_WARN("publish schema failed", K(ret));
    }
  }
  return ret;
}

int ObLocationDDLService::check_location_constraint(const ObTableSchema &schema)
{
  int ret = OB_SUCCESS;
  bool is_odps_external_table = false;
  if (OB_FAIL(sql::ObSQLUtils::is_odps_external_table(&schema, is_odps_external_table))) {
    LOG_WARN("failed to check is odps external table or not", K(ret));
  } else if (is_odps_external_table) {
    // do nothing
  } else {
    if ((!schema.get_external_file_location().empty()
      && OB_INVALID_ID != schema.get_external_location_id())
      || (schema.get_external_file_location().empty()
          && OB_INVALID_ID == schema.get_external_location_id())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("both file location and location id are valid", KR(ret), K(schema));
    }
  }
  return ret;
}
} // end namespace rootserver
} // end namespace oceanbase
