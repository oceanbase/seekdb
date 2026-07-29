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

#define USING_LOG_PREFIX PL_DEPENDENCY
#include "lib/oblog/ob_log_module.h"
#include "ob_pl_dependency_util.h"
#include "ob_pl_resolver.h"
#include "src/sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace share::schema;

namespace pl
{

namespace
{
template <typename DependencyTable>
int check_dep_schema_impl(ObSchemaGetterGuard &schema_guard,
                          const DependencyTable &dep_schema_objs,
                          int64_t merge_version,
                          bool &match)
{
  int ret = OB_SUCCESS;
  match = true;
  for (int64_t i = 0; OB_SUCC(ret) && match && i < dep_schema_objs.count(); ++i) {
    const ObSchemaObjVersion &dep_obj = dep_schema_objs.at(i);
    if (TABLE_SCHEMA == dep_obj.get_schema_type()) {
      const ObSimpleTableSchemaV2 *table_schema = nullptr;
      if (OB_FAIL(schema_guard.get_simple_table_schema(dep_obj.object_id_, table_schema))) {
        LOG_WARN("failed to get table schema", K(ret), K(dep_obj));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null table schema", K(ret), K(dep_obj.object_id_));
      } else if (!table_schema->is_index_table()) {
        match = table_schema->get_schema_version() <= merge_version;
      }
    } else {
      int64_t schema_version = OB_INVALID_VERSION;
      if (OB_FAIL(schema_guard.get_schema_version(dep_obj.get_schema_type(),
                                                  dep_obj.object_id_,
                                                  schema_version))) {
        LOG_WARN("failed to get schema version", K(ret), K(dep_obj));
      } else {
        match = schema_version <= merge_version;
      }
    }
    if (OB_SUCC(ret) && !match) {
      LOG_INFO("dependency schema changed", K(merge_version), K(dep_obj));
    }
  }
  return ret;
}
} // namespace

int ObPLDependencyUtil::add_dependency_object_impl(
    const ObPLDependencyTable *dep_tbl,
    const ObSchemaObjVersion &obj_version)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(dep_tbl)) {
    OZ (add_dependency_object_impl(const_cast<ObPLDependencyTable &>(*dep_tbl), obj_version));
  }
  return ret;
}

int ObPLDependencyUtil::add_dependency_object_impl(
    ObPLDependencyTable &dep_tbl,
    const ObSchemaObjVersion &obj_version)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  for (ObPLDependencyTable::iterator it = dep_tbl.begin(); it < dep_tbl.end(); ++it) {
    if (*it == obj_version) {
      exists = true;
      break;
    }
  }
  if (!exists) {
    OZ (dep_tbl.push_back(obj_version));
  }
  return ret;
}

int ObPLDependencyUtil::add_dependency_objects(
    const ObPLDependencyTable *dep_tbl,
    const ObIArray<ObSchemaObjVersion> &dependency_objects)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(dep_tbl)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < dependency_objects.count(); ++i) {
      OZ (add_dependency_object_impl(dep_tbl, dependency_objects.at(i)));
    }
  }
  return ret;
}

int ObPLDependencyUtil::add_dependency_objects(
    ObPLDependencyTable &dep_tbl,
    const ObPLResolveCtx &resolve_ctx,
    const ObPLDataType &type)
{
  int ret = OB_SUCCESS;
  if (type.is_user_type()) {
    ObSchemaObjVersion obj_version;
    if (type.is_package_type()) {
      const ObSimplePackageSchema *package_info = nullptr;
      const uint64_t package_id = extract_package_id(type.get_user_type_id());
      if (OB_INVALID_ID == package_id || ObTriggerInfo::is_trigger_package_id(package_id)) {
      } else if (OB_FAIL(resolve_ctx.schema_guard_.get_simple_package_info(
                     package_id, package_info))) {
        LOG_WARN("failed to get simple package info",
                 K(ret), K(type), K(package_id), KPC(package_info));
      } else if (OB_ISNULL(package_info)) {
        ret = OB_ERR_PACKAGE_DOSE_NOT_EXIST;
        LOG_WARN("unexpected null package info", K(ret), K(type), K(package_id));
      } else {
        obj_version.object_id_ = package_id;
        obj_version.object_type_ = DEPENDENCY_PACKAGE;
        obj_version.version_ = package_info->get_schema_version();
        if (OB_FAIL(add_dependency_object_impl(dep_tbl, obj_version))) {
          LOG_WARN("failed to add dependency object",
                   K(ret), K(type), KPC(package_info), K(obj_version));
        }
      }
    } else if (type.is_rowtype_type()) {
      const ObSimpleTableSchemaV2 *table_schema = nullptr;
      const uint64_t table_id = type.get_user_type_id();
      if (OB_FAIL(resolve_ctx.schema_guard_.get_simple_table_schema(
              table_id, table_schema))) {
        LOG_WARN("failed to get simple table schema",
                 K(ret), K(type), K(table_id), KPC(table_schema));
      } else if (OB_NOT_NULL(table_schema)) {
        obj_version.object_id_ = table_id;
        obj_version.object_type_ = DEPENDENCY_TABLE;
        obj_version.version_ = table_schema->get_schema_version();
        if (OB_FAIL(add_dependency_object_impl(dep_tbl, obj_version))) {
          LOG_WARN("failed to add dependency object",
                   K(ret), K(type), KPC(table_schema), K(obj_version));
        }
      }
    }
  }
  return ret;
}

int ObPLDependencyUtil::check_dep_schema(share::schema::ObSchemaGetterGuard &schema_guard,
                                         const ObPLDependencyTable &dep_schema_objs,
                                         int64_t merge_version,
                                         bool &match)
{
  return check_dep_schema_impl(schema_guard, dep_schema_objs, merge_version, match);
}

int ObPLDependencyUtil::check_dep_schema(share::schema::ObSchemaGetterGuard &schema_guard,
                                         const sql::DependenyTableStore &dep_schema_objs,
                                         int64_t merge_version,
                                         bool &match)
{
  return check_dep_schema_impl(schema_guard, dep_schema_objs, merge_version, match);
}

} // namespace pl
} // namespace oceanbase
