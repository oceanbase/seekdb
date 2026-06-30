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
#include "share/schema/ob_context_ddl_proxy.h"
#include "share/schema/ob_schema_service_sql_impl.h"

using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

ObContextDDLProxy::ObContextDDLProxy(ObMultiVersionSchemaService &schema_service)
    : schema_service_(schema_service)
{
}

ObContextDDLProxy::~ObContextDDLProxy()
{
}

int ObContextDDLProxy::create_context(
    ObContextSchema &ctx_schema,
    common::ObMySQLTransaction &trans,
    share::schema::ObSchemaGetterGuard &schema_guard,
    const bool or_replace,
    const bool obj_exist,
    const share::schema::ObContextSchema *old_schema,
    bool &need_clean,
    const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  if (or_replace) {
    if (OB_FAIL(create_or_replace_context(ctx_schema, trans, schema_guard, obj_exist,
                                          old_schema, need_clean, ddl_stmt_str))) {
    }
  } else if (obj_exist) {
    ret = OB_ERR_EXIST_OBJECT;
    LOG_WARN("Name is already used by an existing object", K(ret), K(ctx_schema));
  } else if (OB_FAIL(inner_create_context(ctx_schema, trans, schema_guard,
                                          ddl_stmt_str))) {
  }
  return ret;
}

int ObContextDDLProxy::inner_create_context(
    ObContextSchema &ctx_schema,
    common::ObMySQLTransaction &trans,
    share::schema::ObSchemaGetterGuard &schema_guard,
    const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  uint64_t new_context_id = OB_INVALID_ID;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("schema_service must not null", K(ret));
  } else if (OB_FAIL(schema_service->fetch_new_context_id(new_context_id))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    ctx_schema.set_schema_version(new_schema_version);
    ctx_schema.set_context_id(new_context_id);
    if (OB_FAIL(schema_service->get_context_sql_service().insert_context(
                ctx_schema, &trans, ddl_stmt_str))) {
    }
  }
  return ret;
}

int ObContextDDLProxy::drop_context(
    share::schema::ObContextSchema &ctx_schema,
    common::ObMySQLTransaction &trans,
    share::schema::ObSchemaGetterGuard &schema_guard,
    const share::schema::ObContextSchema *old_schema,
    bool &need_clean,
    const common::ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  uint64_t context_id = OB_INVALID_ID;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("schema_service must not null", K(ret));
  } else if (OB_ISNULL(old_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get old schema", K(ret));
  } else if (OB_INVALID_ID != old_schema->get_context_id()) {
    ctx_schema.set_context_id(old_schema->get_context_id());
    ctx_schema.set_context_type(old_schema->get_context_type());
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get context id", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_context_sql_service().drop_context(
              ctx_schema, new_schema_version, &trans, need_clean, ddl_stmt_str))) {
  }
  return ret;
}

int ObContextDDLProxy::create_or_replace_context(
    ObContextSchema &ctx_schema,
    common::ObMySQLTransaction &trans,
    share::schema::ObSchemaGetterGuard &schema_guard,
    const bool obj_exist,
    const ObContextSchema *old_schema,
    bool &need_clean,
    const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  bool is_replace = false;
  uint64_t new_context_id = OB_INVALID_ID;
  need_clean = false;
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("schema_service must not null", K(ret));
  } else if (obj_exist) {
    ObContextSchema tmp_schema;
    if (OB_ISNULL(old_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get context id", K(ret));
    } else if (OB_FAIL(tmp_schema.assign(*old_schema))) {
    } else if (old_schema->get_context_type() != ctx_schema.get_context_type()) {
      if (OB_FAIL(drop_context(tmp_schema, trans, schema_guard, old_schema,
                               need_clean, nullptr))) {
      } else if (OB_FAIL(inner_create_context(ctx_schema, trans, schema_guard, ddl_stmt_str))) {
      }
    } else if (FALSE_IT(ctx_schema.set_context_id(old_schema->get_context_id()))) {
    } else if (OB_FAIL(inner_alter_context(ctx_schema, trans, schema_guard, ddl_stmt_str))) {
    }
  } else if (OB_FAIL(inner_create_context(ctx_schema, trans, schema_guard,
                                          ddl_stmt_str))) {
  }
  return ret;
}

int ObContextDDLProxy::inner_alter_context(
    ObContextSchema &ctx_schema,
    common::ObMySQLTransaction &trans,
    share::schema::ObSchemaGetterGuard &schema_guard,
    const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  uint64_t new_context_id = OB_INVALID_ID;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("schema_service must not null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    ctx_schema.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_context_sql_service().alter_context(
                ctx_schema, &trans, ddl_stmt_str))) {
    }
  }
  return ret;
}
