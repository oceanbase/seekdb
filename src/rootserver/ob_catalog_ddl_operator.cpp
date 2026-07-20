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
#include "rootserver/ob_catalog_ddl_operator.h"
#include "rootserver/ob_ddl_operator.h"
#include "src/share/schema/ob_priv_sql_service.h"

namespace oceanbase
{
namespace rootserver
{

int ObCatalogDDLOperator::handle_catalog_function(ObCatalogSchema &schema,
                                                  ObMySQLTransaction &trans,
                                                  const share::schema::ObSchemaOperationType ddl_type,
                                                  const ObString &ddl_stmt_str,
                                                  bool if_exist,
                                                  bool if_not_exist,
                                                  ObSchemaGetterGuard &schema_guard,
                                                  uint64_t user_id)
{
  int ret = OB_SUCCESS;
  
  ObSchemaService *schema_sql_service = NULL;
  int64_t new_schema_version = OB_INVALID_VERSION;
  uint64_t catalog_id = OB_INVALID_ID;
  bool is_schema_exist = false;
  const ObCatalogSchema *old_schema = NULL;
  bool need_apply_schema = true;
  if (OB_ISNULL(schema_sql_service = schema_service_.get_schema_service())) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_sql_service must not null", K(ret));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      LOG_WARN("fail to gen new schema_version", K(ret));
    } else {
      schema.set_schema_version(new_schema_version);
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(schema_guard.get_catalog_schema_by_name(
                                                        schema.get_catalog_name_str(),
                                                        old_schema))) {
      LOG_WARN("fail to check schema exist", K(ret));
    } else {
      is_schema_exist = (old_schema != NULL);
    }
  }
  if (OB_SUCC(ret)) {
    switch (ddl_type)
    {
      case OB_DDL_CREATE_CATALOG:
      {
        if (OB_UNLIKELY(is_schema_exist)) {
          if (if_not_exist) {
            need_apply_schema = false;
            LOG_USER_NOTE(OB_CATALOG_EXIST, schema.get_catalog_name_str().length(),
                          schema.get_catalog_name_str().ptr());
            LOG_WARN("catalog already exist", K(ret));
          } else {
            ret = OB_CATALOG_EXIST;
            LOG_USER_ERROR(OB_CATALOG_EXIST, schema.get_catalog_name_str().length(),
                          schema.get_catalog_name_str().ptr());
            LOG_WARN("catalog already exist", K(ret));
          }
        } else if (OB_FAIL(schema_sql_service->fetch_new_catalog_id(catalog_id))) {
          LOG_WARN("fail to fetch new catalog id", K(ret));
        } else {
          schema.set_catalog_id(catalog_id);
        }
        break;
      }
      case OB_DDL_DROP_CATALOG:
      case OB_DDL_ALTER_CATALOG:
      {
        if (OB_UNLIKELY(!is_schema_exist)) {
          if (if_exist && OB_DDL_DROP_CATALOG == ddl_type) {
            need_apply_schema = false;
            LOG_USER_NOTE(OB_CATALOG_NOT_EXIST, schema.get_catalog_name_str().length(),
                          schema.get_catalog_name_str().ptr());
            LOG_WARN("catalog not exist", K(ret));
          } else {
            ret = OB_CATALOG_NOT_EXIST;
            LOG_USER_ERROR(OB_CATALOG_NOT_EXIST, schema.get_catalog_name_str().length(),
                          schema.get_catalog_name_str().ptr());
            LOG_WARN("catalog not exist", K(ret));
          }
        } else {
          schema.set_catalog_id(old_schema->get_catalog_id());
          schema.set_catalog_name(old_schema->get_catalog_name_str());
        }
        break;
      }
      default:
      {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not support ddl type", K(ret), K(ddl_type));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "this catalog ddl type");
      }
    }
  }
  if (OB_SUCC(ret) && need_apply_schema) {
    if (OB_FAIL(schema_sql_service->get_catalog_sql_service()
                      .apply_new_schema(schema, trans, ddl_type, ddl_stmt_str))) {
      LOG_WARN("apply catalog failed", K(ret));
    }
  }
  // after catalog created, we should grant privilege to creator
  if (OB_SUCC(ret) && need_apply_schema
      && OB_FAIL(grant_or_revoke_after_ddl(schema, trans, ddl_type, schema_guard, user_id))) {
    LOG_WARN("failed to grant or revoke after ddl", K(ret));
  }
  return ret;
}

int ObCatalogDDLOperator::grant_or_revoke_after_ddl(ObCatalogSchema &schema,
                                                    ObMySQLTransaction &trans,
                                                    const share::schema::ObSchemaOperationType ddl_type,
                                                    ObSchemaGetterGuard &schema_guard,
                                                    uint64_t user_id)
{
  int ret = OB_SUCCESS;
  if (ddl_type == OB_DDL_CREATE_CATALOG) {
    ObCatalogPrivSortKey catalog_priv_key(user_id, schema.get_catalog_name_str());
    ObPrivSet priv_set = OB_PRIV_USE_CATALOG;
    OZ(grant_revoke_catalog(catalog_priv_key, priv_set, true, ObString(), trans));
  }
  if (OB_SUCC(ret) && ddl_type == OB_DDL_DROP_CATALOG) {
  }
  return ret;
}

int ObCatalogDDLOperator::grant_revoke_catalog(const ObCatalogPrivSortKey &catalog_priv_key,
                                               const ObPrivSet priv_set,
                                               const bool grant,
                                               const common::ObString &ddl_stmt_str,
                                               common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = NULL;
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObPrivSet new_priv = priv_set;
  bool need_flush = true;
  ObPrivSet catalog_priv_set = OB_PRIV_SET_EMPTY;
  
  uint64_t user_id = catalog_priv_key.user_id_;
  if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid user_id", K(user_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service = schema_service_.get_schema_service())) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_sql_service must not null", K(ret));
  } else if (0 == priv_set) {
    //do nothing
  } else if (OB_FAIL(schema_service_.get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_catalog_priv_set(catalog_priv_key,
                                                       catalog_priv_set))) {
    LOG_WARN("get catalog priv set failed", K(ret));
  } else if (!grant && OB_PRIV_SET_EMPTY == catalog_priv_set) {
    ret = OB_ERR_NO_GRANT;
    LOG_WARN("no such grant to revoke", K(ret));
  } else {
    if (grant) {
      new_priv = priv_set | catalog_priv_set;
    } else {
      new_priv = (~priv_set) & catalog_priv_set;
    }
    need_flush = (new_priv != catalog_priv_set);
    if (need_flush) {
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        LOG_WARN("failed to gen new schema_version", K(ret));
      } else if (OB_FAIL(schema_sql_service->get_catalog_sql_service()
                              .grant_revoke_catalog(catalog_priv_key, new_priv, new_schema_version,
                                                    ddl_stmt_str, trans))) {
        LOG_WARN("apply catalog failed", K(ret));
      }
    }
  }
  return ret;
}

}
}
