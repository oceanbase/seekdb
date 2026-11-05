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
#include "ob_dependency_ddl_helper.h"
#include "ob_ddl_operator.h"
#include "share/schema/ob_dependency_info.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_schema_utils.h"
#include "share/schema/ob_table_schema.h"
#include "share/ob_dml_sql_splicer.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
namespace rootserver
{

int ObDependencyDDLHelper::modify_dep_obj_status(common::ObMySQLTransaction &trans,
                                            uint64_t obj_id,
                                            rootserver::ObDDLOperator &ddl_operator,
                                            share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(cascading_modify_obj_status(trans, obj_id,
                                                 ddl_operator,
                                                 schema_service))) {
    LOG_WARN("failed to modify obj status", K(ret));
  }
  return ret;
}

int ObDependencyDDLHelper::cascading_modify_obj_status(common::ObMySQLTransaction &trans,
                                                  uint64_t obj_id,
                                                  rootserver::ObDDLOperator &ddl_operator,
                                                  share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  ObArray<std::pair<uint64_t, share::schema::ObObjectType>> objs;
  if (OB_FAIL(ObDependencyInfo::collect_all_dep_objs(obj_id, trans, objs))) {
    LOG_WARN("failed to collect all objs", K(ret));
  } else if (OB_FAIL(modify_all_obj_status(objs, trans, ddl_operator, schema_service))) {
    LOG_WARN("failed to modify obj status", K(ret));
  }
  return ret;
}

int ObDependencyDDLHelper::modify_all_obj_status(const ObIArray<std::pair<uint64_t, share::schema::ObObjectType>> &objs,
                                            common::ObMySQLTransaction &trans,
                                            rootserver::ObDDLOperator &ddl_operator,
                                            share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  const bool update_object_status_ignore_version = false;
  if (OB_ISNULL(schema_service.get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get schema service", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < objs.count(); ++i) {
    if (OB_INVALID_ID == objs.at(i).first) {
      // skipped by ddl
      continue;
    }
    if (OB_SUCC(ret)) {
      ObRefreshSchemaStatus schema_status;
      ObObjectStatus new_status = ObObjectStatus::INVALID;
      int64_t refresh_schema_version = OB_INVALID_SCHEMA_VERSION;
      if (share::schema::ObObjectType::VIEW == objs.at(i).second) {
        HEAP_VAR(ObTableSchema, view_schema) {
          if (OB_FAIL(schema_service.get_schema_service()->get_table_schema_from_inner_table(schema_status, objs.at(i).first, trans, view_schema))) {
            LOG_WARN("failed to get view schema", K(ret));
          } else if (!view_schema.is_view_table()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get wrong schema", K(ret), K(view_schema));
          } else if (new_status == view_schema.get_object_status()) {
          } else if (OB_FAIL(schema_service.gen_new_schema_version(refresh_schema_version))) {
            LOG_WARN("fail to gen new schema_version", K(ret));
          } else if (OB_FAIL(ddl_operator.update_table_status(view_schema, refresh_schema_version,
                                                              new_status, update_object_status_ignore_version,
                                                              trans))) {
            LOG_WARN("failed to update table status", K(ret));
          }
        }
      } else if (share::schema::ObObjectType::SYNONYM == objs.at(i).second) {
        // TODO:peihan.dph
      }
    }
  }
  return ret;
}

int ObDependencyDDLHelper::batch_fill_kv_pairs(
    const ObReferenceObjTable::ObDependencyObjKey &dep_obj_key,
    const int64_t new_schema_version,
    common::ObIArray<ObDependencyInfo> &dep_infos,
    share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0 ; OB_SUCC(ret) && i < dep_infos.count(); ++i) {
    ObDependencyInfo & dep = dep_infos.at(i);
    dep.set_dep_obj_id(dep_obj_key.dep_obj_id_);
    dep.set_dep_obj_owner_id(dep_obj_key.dep_obj_id_);
    dep.set_schema_version(new_schema_version);
    if (OB_FAIL(dep.gen_dependency_dml(dml))) {
      LOG_WARN("gen table dml failed", K(ret));
    } else if (OB_FAIL(dml.finish_row())) {
      LOG_WARN("failed to finish row", K(ret));
    }
  }
  return ret;
}

int ObDependencyDDLHelper::batch_execute_insert_or_update_obj_dependency(
    const int64_t new_schema_version,
    const ObReferenceObjTable::DependencyObjKeyItemPairs &dep_objs,
    ObMySQLTransaction &trans,
    share::schema::ObSchemaGetterGuard &schema_guard,
    rootserver::ObDDLOperator &ddl_operator)
{
  int ret = OB_SUCCESS;
  {
    ObSqlString sql;
    ObDMLSqlSplicer dml;
    int64_t affected_rows = 0;
    for (int64_t i = 0 ; OB_SUCC(ret) && i < dep_objs.count(); ++i) {
      ObSArray<ObDependencyInfo> dep_infos;
      ObString dummy;
      const ObReferenceObjTable::ObDependencyObjKey &dep_obj_key = dep_objs.at(i).dep_obj_key_;
      const ObReferenceObjTable::ObDependencyObjItem &dep_obj_item = dep_objs.at(i).dep_obj_item_;
      if (!dep_obj_key.is_valid()
          || OB_INVALID_SCHEMA_VERSION == dep_obj_item.max_ref_obj_schema_version_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("illegal schema version or dependency obj key", K(ret), K(dep_obj_key),
        K(dep_obj_item.max_ref_obj_schema_version_));
      } else if (OB_FAIL(ObDependencyInfo::collect_dep_infos(
                  dep_obj_item.get_ref_obj_versions(),
                  dep_infos,
                  dep_obj_key.dep_obj_type_,
                  0, dummy, dummy, false/* is_pl */))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to collect dependency infos", K(ret));
      } else if (OB_FAIL(batch_fill_kv_pairs(dep_obj_key,
                 new_schema_version, dep_infos, dml))) {
        LOG_WARN("failed to batch fill kv pairs", K(ret), K(dep_obj_key));
      } else if (OB_FAIL(update_max_dependency_version(dep_obj_key.dep_obj_id_, dep_obj_item.max_ref_obj_schema_version_,
                 trans, schema_guard, ddl_operator))) {
        LOG_WARN("failed to update max dependency version", K(ret), K(dep_obj_key));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(dml.splice_batch_insert_update_sql(OB_ALL_DEPENDENCY_TNAME, sql))) {
      LOG_WARN("splice sql failed", K(ret));
    } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
      LOG_WARN("execute sql failed", K(sql), K(ret));
    } else {
      LOG_DEBUG("execute sql dml succ", K(sql));
    }
  }
  return ret;
}

int ObDependencyDDLHelper::update_max_dependency_version(
    const int64_t dep_obj_id,
    const int64_t max_dependency_version,
    ObMySQLTransaction &trans,
    ObSchemaGetterGuard &schema_guard,
    rootserver::ObDDLOperator &ddl_operator)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = nullptr;
  ObTableSchema new_table_schema;
  if (OB_FAIL(schema_guard.get_table_schema(dep_obj_id, table_schema))) {
    LOG_WARN("get_table_schema failed", "table id", dep_obj_id, KR(ret));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table schema should not be null", KR(ret));
  } else if (OB_FAIL(new_table_schema.assign(*table_schema))) {
    LOG_WARN("fail to assign schema", K(ret));
  } else {
    new_table_schema.set_max_dependency_version(max_dependency_version);
    ObSchemaOperationType operation_type = OB_DDL_ALTER_TABLE;
    if (OB_FAIL(ddl_operator.update_table_attribute(new_table_schema,
                                                    trans,
                                                    operation_type))) {
      LOG_WARN("failed to update data table schema attribute", K(ret));
    }
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
