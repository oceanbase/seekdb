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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_dependency_info.h"
#include "lib/utility/ob_smart_call.h"  // SMART_CALL, previously hidden behind the exec_context include chain, make the dependency explicit

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{

ObDependencyInfo::ObDependencyInfo()
{
  reset();
}

ObDependencyInfo::ObDependencyInfo(ObIAllocator *allocator)
  : ObSchema(allocator)
{
  reset();
}

ObDependencyInfo::ObDependencyInfo(const ObDependencyInfo &src_schema)
  : ObSchema()
{
  reset();
  *this = src_schema;
}

ObDependencyInfo::~ObDependencyInfo()
{
}

ObDependencyInfo &ObDependencyInfo::operator =(const ObDependencyInfo &src_schema)
{
  if (this != &src_schema) {
    reset();
    int &ret = error_ret_;
    dep_obj_id_ = src_schema.dep_obj_id_;
    dep_obj_type_ = src_schema.dep_obj_type_;
    order_ = src_schema.order_;
    dep_timestamp_ = src_schema.dep_timestamp_;
    ref_obj_id_ = src_schema.ref_obj_id_;
    ref_obj_type_ = src_schema.ref_obj_type_;
    ref_timestamp_ = src_schema.ref_timestamp_;
    dep_obj_owner_id_ = src_schema.dep_obj_owner_id_;
    property_ = src_schema.property_;
    schema_version_ = src_schema.schema_version_;
    if (OB_FAIL(deep_copy_str(src_schema.dep_attrs_, dep_attrs_))) {
    } else if (OB_FAIL(deep_copy_str(src_schema.dep_reason_, dep_reason_))) {
    } else if (OB_FAIL(deep_copy_str(src_schema.ref_obj_name_, ref_obj_name_))) {
    }
    error_ret_ = ret;
  }
  return *this;
}

int ObDependencyInfo::assign(const ObDependencyInfo &other)
{
  int ret = OB_SUCCESS;
  this->operator=(other);
  ret = this->error_ret_;
  return ret;
}

bool ObDependencyInfo::is_user_field_valid() const
{
  bool ret = false;
  if (ObSchema::is_valid()) {
    ret = true;
  }
  return ret;
}

bool ObDependencyInfo::is_valid() const
{
  bool ret = false;
  if (ObSchema::is_valid()) {
    if (is_user_field_valid()) {
      ret = (OB_INVALID_ID != dep_obj_id_)
          && (OB_INVALID_ID != ref_obj_id_)
          && (OB_INVALID_VERSION != schema_version_);
    } else {}
  } else {}
  return ret;
}


int ObDependencyInfo::gen_dependency_dml(ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;

  const ObDependencyInfo &dep_info = *this;
  if (OB_FAIL(dml.add_pk_column("dep_obj_id", extract_obj_id(dep_info.get_dep_obj_id())))
    || OB_FAIL(dml.add_pk_column("dep_obj_type", dep_info.get_dep_obj_type()))
    || OB_FAIL(dml.add_pk_column("dep_order", dep_info.get_order()))
    || OB_FAIL(dml.add_column("schema_version", dep_info.get_schema_version()))
    || OB_FAIL(dml.add_time_column("dep_timestamp", dep_info.get_dep_timestamp()))
    || OB_FAIL(dml.add_column("ref_obj_id", get_ref_obj_id()))
    || OB_FAIL(dml.add_column("ref_obj_type", dep_info.get_ref_obj_type()))
    || OB_FAIL(dml.add_time_column("ref_timestamp", dep_info.get_ref_timestamp()))
    || OB_FAIL(dml.add_column("dep_obj_owner_id", extract_obj_id(dep_info.get_dep_obj_owner_id())))
    || OB_FAIL(dml.add_column("property", dep_info.get_property()))
    || OB_FAIL(dml.add_column("dep_attrs", ObHexEscapeSqlStr(dep_info.get_dep_attrs())))
    || OB_FAIL(dml.add_column("dep_reason", ObHexEscapeSqlStr(dep_info.get_dep_reason())))
    || OB_FAIL(dml.add_column("ref_obj_name", ObHexEscapeSqlStr(dep_info.get_ref_obj_name())))
    || OB_FAIL(dml.add_gmt_create())
    || OB_FAIL(dml.add_gmt_modified())) {
    LOG_WARN("add column failed", K(ret));
  }
  return ret;
}

uint64_t ObDependencyInfo::extract_obj_id(uint64_t id)
{
  return ObSchemaUtils::get_extract_schema_id(id);
}

int ObDependencyInfo::parse_from(common::sqlclient::ObMySQLResult &result)
{
  int ret = OB_SUCCESS;
  reset();
  ObDependencyInfo &dep = *this;

  EXTRACT_INT_FIELD_TO_CLASS_MYSQL(result, dep_obj_id, dep, uint64_t);
  EXTRACT_INT_FIELD_TO_CLASS_MYSQL(result, dep_obj_type, dep, ObObjectType);
  EXTRACT_INT_FIELD_TO_CLASS_VALUE_MYSQL(result, dep_order, order, dep, uint64_t);
  EXTRACT_INT_FIELD_TO_CLASS_MYSQL(result, schema_version, dep, int64_t);
  EXTRACT_INT_FIELD_TO_CLASS_MYSQL(result, dep_timestamp, dep, int64_t);
  EXTRACT_INT_FIELD_TO_CLASS_MYSQL(result, ref_obj_type, dep, ObObjectType);
  EXTRACT_INT_FIELD_TO_CLASS_MYSQL(result, ref_obj_id, dep, uint64_t);
  EXTRACT_INT_FIELD_TO_CLASS_MYSQL(result, ref_timestamp, dep, int64_t);
  EXTRACT_INT_FIELD_TO_CLASS_MYSQL_WITH_DEFAULT_VALUE(result, dep_obj_owner_id, dep, uint64_t, true, false, OB_INVALID_ID);
  EXTRACT_INT_FIELD_TO_CLASS_MYSQL(result, property, dep, uint64_t);
  EXTRACT_VARCHAR_FIELD_TO_CLASS_MYSQL_SKIP_RET(result, dep_attrs, dep);
  EXTRACT_VARCHAR_FIELD_TO_CLASS_MYSQL_SKIP_RET(result, dep_reason, dep);
  EXTRACT_VARCHAR_FIELD_TO_CLASS_MYSQL_SKIP_RET(result, ref_obj_name, dep);
  return ret;
}

int ObDependencyInfo::delete_schema_object_dependency(common::ObISQLClient &trans,
                                                      uint64_t dep_obj_id,
                                                      int64_t schema_version,
                                                      ObObjectType dep_obj_type)
{
  UNUSED(schema_version);
  int ret = OB_SUCCESS;

  ObSqlString sql;
  int64_t affected_rows = 0;
  if (OB_INVALID_ID == dep_obj_id
    || ObObjectType::MAX_TYPE == dep_obj_type) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("delete error info unexpected.", K(ret),
                                              K(dep_obj_id), K(dep_obj_type));
  } else if (sql.assign_fmt("delete FROM %s WHERE dep_obj_id = %ld \
                                                  AND dep_obj_type = %ld",
            OB_ALL_DEPENDENCY_TNAME,
            extract_obj_id(dep_obj_id),
            static_cast<uint64_t>(dep_obj_type))) {
    LOG_WARN("delete from __all_dependency table failed.", K(ret),
                                                                  K(dep_obj_id),
                                                                  K(dep_obj_type));
  } else {
    if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
    } else {
      // do nothing
    }
  }
  return ret;
}

int ObDependencyInfo::insert_schema_object_dependency(common::ObISQLClient &trans,
                                                      bool is_replace, bool only_history)
{
  int ret = OB_SUCCESS;
  ObDependencyInfo& dep_info = *this;

  ObDMLSqlSplicer dml;
  // This block remains disabled until the __all_package virtual table is implemented.
  //int64_t ref_obj_create_time = -1;
  //ObString ref_obj_name;
  // OZ (get_object_create_time(trans, dep_info.get_ref_obj_type(),
  // ref_obj_create_time, ref_obj_name));
  // OX (dep_info.set_ref_timestamp(ref_obj_create_time));
  // OZ (dep_info.set_ref_obj_name(ref_obj_name));
  if (OB_FAIL(ret)) {
  } else if (get_dep_obj_id() == get_ref_obj_id() && get_dep_obj_type() == get_ref_obj_type()) {
    // rule out self reference
  } else if (OB_FAIL(gen_dependency_dml(dml))) {
  } else {
    ObDMLExecHelper exec(trans);
    int64_t affected_rows = 0;
    if (!only_history) {
      ObDMLExecHelper exec(trans);
      if (is_replace) {
        if (OB_FAIL(exec.exec_insert_update(OB_ALL_DEPENDENCY_TNAME, dml, affected_rows))) {
        }
      } else {
        if (OB_FAIL(exec.exec_insert(OB_ALL_DEPENDENCY_TNAME, dml, affected_rows))) {
        }
      }
      if (OB_SUCC(ret) && !is_single_row(affected_rows) && !is_double_row(affected_rows)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("affected_rows unexpected to be one", K(affected_rows), K(ret));
      }
    }
  }
  return ret;
}

int ObDependencyInfo::collect_dep_info(ObIArray<ObDependencyInfo> &deps,
                                       ObObjectType dep_obj_type,
                                       int64_t ref_obj_id,
                                       int64_t ref_timestamp,
                                       ObDependencyTableType dependent_type)
{
  int ret = OB_SUCCESS;
  const ObObjectType ref_obj_type = ObSchemaObjVersion::get_schema_object_type(dependent_type);
  // omit duplicate dependent objects
  bool exist = false;
  for (int i = 0; i < deps.count(); i++) {
    const ObDependencyInfo& tmp_dep = deps.at(i);
    if (tmp_dep.get_dep_obj_type() == dep_obj_type
        && tmp_dep.get_ref_obj_id() == ref_obj_id
        && tmp_dep.get_ref_timestamp() == ref_timestamp
        && tmp_dep.get_ref_obj_type() == ref_obj_type) {
      exist = true;
      break;
    }
  }
  if (OB_SUCC(ret) && !exist) {
    ObDependencyInfo dep;
    dep.set_dep_obj_id(OB_INVALID_ID);
    dep.set_dep_obj_type(dep_obj_type);
    dep.set_dep_obj_owner_id(OB_INVALID_ID);
    dep.set_ref_obj_id(ref_obj_id);
    dep.set_ref_obj_type(ref_obj_type);
    dep.set_order(deps.count());
    dep.set_dep_timestamp(-1);
    dep.set_ref_timestamp(ref_timestamp);
    dep.set_property(0);
    ObString dummy;
    dep.set_dep_attrs(dummy);
    dep.set_dep_reason(dummy);
    OZ(deps.push_back(dep));
  }
  return ret;
}

int ObDependencyInfo::collect_dep_infos(const ObIArray<ObSchemaObjVersion> &schema_objs,
                               ObIArray<ObDependencyInfo> &deps,
                               ObObjectType dep_obj_type,
                               uint64_t property,
                               ObString &dep_attrs,
                               ObString &dep_reason,
                               bool is_pl)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < schema_objs.count(); ++i) {
    const ObSchemaObjVersion &s_objs = schema_objs.at(i);
    const ObObjectType ref_obj_type = ObSchemaObjVersion::get_schema_object_type(
        s_objs.object_type_);
    if (0 == i && ObObjectType::TRIGGER == dep_obj_type) {
      // For a trigger, schema_objs.at(0) is itself;
      // need to skip to avoid self reference.
      continue;
    } else if (!s_objs.is_valid()) {
      // omit invalid dependency
      continue;
    } else {
      // omit duplicate dependent objects
      bool exist = false;
      for (int i = 0; i < deps.count(); i++) {
        const ObDependencyInfo& tmp_dep = deps.at(i);
        if (tmp_dep.get_dep_obj_type() == dep_obj_type
            && tmp_dep.get_ref_obj_id() == s_objs.get_object_id()
            && tmp_dep.get_ref_timestamp() == s_objs.get_version()
            && tmp_dep.get_ref_obj_type() == ref_obj_type) {
          exist = true;
          break;
        }
      }
      if (exist) {
        continue;
      }
    }

    ObDependencyInfo dep;
    dep.set_dep_obj_id(OB_INVALID_ID);
    dep.set_dep_obj_type(dep_obj_type);
    dep.set_dep_obj_owner_id(OB_INVALID_ID);
    dep.set_ref_obj_id(s_objs.get_object_id());
    dep.set_ref_obj_type(ref_obj_type);
    dep.set_order(deps.count());
    dep.set_dep_timestamp(-1);
    dep.set_ref_timestamp(s_objs.get_version());
    dep.set_property(property);
    if (dep_attrs.length() >= OB_MAX_RAW_SQL_COL_LENGTH
        || dep_reason.length() >= OB_MAX_RAW_SQL_COL_LENGTH) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dep attrs or dep reason is too long", K(ret),
               K(dep_attrs.length()), K(dep_reason.length()));
    } else {
      if (!dep_attrs.empty())
        OZ(dep.set_dep_attrs(dep_attrs));
      if (!dep_reason.empty())
        OZ(dep.set_dep_reason(dep_reason));
    }
    OZ(deps.push_back(dep));
  }
  return ret;
}

int ObDependencyInfo::collect_dep_infos(ObReferenceObjTable &ref_objs,
                                        ObIArray<ObDependencyInfo> &deps,
                                        ObObjectType dep_obj_type,
                                        uint64_t dep_obj_id,
                                        int64_t &max_version)
{
  int ret = OB_SUCCESS;
  int64_t order = 0;
  max_version = OB_INVALID_VERSION;
  auto &ref_obj_map = ref_objs.get_ref_obj_table();
  for (auto it = ref_obj_map.begin(); OB_SUCC(ret) && it != ref_obj_map.end(); ++it) {
    ObDependencyInfo dep;
    uint64_t curr_dep_obj_id = it->first.dep_obj_id_;
    // create view path, only record directly dependency
    if (curr_dep_obj_id == dep_obj_id) {
      for (int64_t i = 0; OB_SUCC(ret) && i < it->second->ref_obj_versions_.count(); ++i) {
        ObDependencyInfo dep;
        max_version = std::max(it->second->ref_obj_versions_.at(i).version_, max_version);
        dep.set_dep_obj_id(OB_INVALID_ID);
        dep.set_dep_obj_type(it->first.dep_obj_type_);
        dep.set_dep_obj_owner_id(it->first.dep_db_id_);
        dep.set_ref_obj_id(it->second->ref_obj_versions_.at(i).object_id_);
        dep.set_ref_obj_type(ObSchemaObjVersion::get_schema_object_type(it->second->ref_obj_versions_.at(i).object_type_));
        dep.set_order(order);
        ++order;
        dep.set_dep_timestamp(-1);
        dep.set_ref_timestamp(it->second->ref_obj_versions_.at(i).version_);
        OZ (deps.push_back(dep));
      }
    }
  }
  
  return ret;
}

int ObDependencyInfo::collect_dep_infos(
    const ObIArray<ObBasedSchemaObjectInfo> &based_schema_object_infos,
    ObIArray<ObDependencyInfo> &deps,
    const ObObjectType dep_obj_type,
    const uint64_t dep_obj_id,
    const uint64_t dep_obj_owner_id,
    const uint64_t property,
    const ObString &dep_attrs,
    const ObString &dep_reason,
    const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  int64_t order = 0;
  deps.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < based_schema_object_infos.count(); ++i) {
    const ObBasedSchemaObjectInfo &base_info = based_schema_object_infos.at(i);
    if (OB_UNLIKELY(TABLE_SCHEMA != base_info.schema_type_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid based schema object", KR(ret), K(base_info));
    } else {
      ObDependencyInfo dep;

      dep.set_dep_obj_type(dep_obj_type);
      dep.set_dep_obj_id(dep_obj_id);
      dep.set_order(order++);
      dep.set_schema_version(schema_version);
      dep.set_dep_timestamp(-1);
      dep.set_ref_obj_type(ObObjectType::TABLE);
      dep.set_ref_obj_id(base_info.schema_id_);
      dep.set_ref_timestamp(base_info.schema_version_);
      dep.set_dep_obj_owner_id(dep_obj_owner_id);
      dep.set_property(property);
      if (!dep_attrs.empty() && OB_FAIL(dep.set_dep_attrs(dep_attrs))) {
        LOG_WARN("fail to set dep attrs", KR(ret), K(dep_attrs));
      } else if (!dep_reason.empty() && OB_FAIL(dep.set_dep_reason(dep_reason))) {
        LOG_WARN("fail to set dep reason", KR(ret), K(dep_attrs));
      } else if (OB_FAIL(deps.push_back(dep))) {
      }
    }
  }
  return ret;
}

int ObDependencyInfo::collect_ref_infos(uint64_t dep_obj_id,
                                        common::ObISQLClient &sql_proxy,
                                        common::ObIArray<ObDependencyInfo> &deps)
{
  int ret = OB_SUCCESS;
  deps.reset();

  SMART_VAR(common::ObMySQLProxy::MySQLResult, res)
  {
    common::sqlclient::ObMySQLResult *result = nullptr;
    ObSqlString sql;
    if (OB_FAIL(sql.assign_fmt("SELECT * FROM %s WHERE dep_obj_id = %lu ORDER BY dep_order",
                               OB_ALL_DEPENDENCY_TNAME,
                               dep_obj_id))) {
    } else if (OB_FAIL(sql_proxy.read(res, sql.ptr()))) {
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", K(ret));
    } else {
      while (OB_SUCC(ret)) {
        if (OB_FAIL(result->next())) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("fail to get next", KR(ret));
          } else {
            ret = OB_SUCCESS;
            break;
          }
        } else {
          ObDependencyInfo dep;
          if (OB_FAIL(dep.parse_from(*result))) {
          } else if (OB_FAIL(deps.push_back(dep))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObDependencyInfo::collect_dep_infos(uint64_t ref_obj_id,
                                        common::ObISQLClient &sql_proxy,
                                        common::ObIArray<ObDependencyInfo> &deps)
{
  int ret = OB_SUCCESS;
  deps.reset();

  SMART_VAR(common::ObMySQLProxy::MySQLResult, res)
  {
    common::sqlclient::ObMySQLResult *result = nullptr;
    ObSqlString sql;
    if (OB_FAIL(sql.assign_fmt("SELECT * FROM %s WHERE ref_obj_id = %lu",
                               OB_ALL_DEPENDENCY_TNAME,
                               ref_obj_id))) {
    } else if (OB_FAIL(sql_proxy.read(res, sql.ptr()))) {
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", K(ret));
    } else {
      while (OB_SUCC(ret)) {
        if (OB_FAIL(result->next())) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("fail to get next", KR(ret));
          } else {
            ret = OB_SUCCESS;
            break;
          }
        } else {
          ObDependencyInfo dep;
          if (OB_FAIL(dep.parse_from(*result))) {
          } else if (OB_FAIL(deps.push_back(dep))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObDependencyInfo::collect_all_dep_objs(uint64_t ref_obj_id,
                                           common::ObISQLClient &sql_proxy,
                                           common::ObIArray<std::pair<uint64_t, share::schema::ObObjectType>> &objs)
{
  return collect_all_dep_objs_inner(ref_obj_id, ref_obj_id, sql_proxy, objs);
}

int ObDependencyInfo::collect_all_dep_objs_inner(uint64_t root_obj_id,
                                                 uint64_t ref_obj_id,
                                                 common::ObISQLClient &sql_proxy,
                                                 common::ObIArray<std::pair<uint64_t, share::schema::ObObjectType>> &objs)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;

  const int64_t init_count = objs.count();
  {
    HEAP_VAR(common::ObMySQLProxy::MySQLResult, res) {
      common::sqlclient::ObMySQLResult *result = NULL;
      if (OB_FAIL(sql.assign_fmt("SELECT dep_obj_id, dep_obj_type FROM %s WHERE ref_obj_id = %lu",
                                        OB_ALL_DEPENDENCY_TNAME,
                                        ref_obj_id))) {
      } else if (OB_FAIL(sql_proxy.read(res, sql.ptr()))) {
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is null", K(ret));
      } else {
        while (OB_SUCC(result->next())) {       
          int64_t tmp_obj_id = OB_INVALID_ID;
          int64_t tmp_type = static_cast<int64_t> (share::schema::ObObjectType::INVALID);
          EXTRACT_INT_FIELD_MYSQL(*result, "dep_obj_id", tmp_obj_id, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "dep_obj_type", tmp_type, int64_t);
          if (OB_FAIL(ret)) {
          } else if (tmp_type <= static_cast<int64_t> (share::schema::ObObjectType::INVALID)
                      || tmp_type >= static_cast<int64_t> (share::schema::ObObjectType::MAX_TYPE)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get wrong obj type", K(ret));
          } else if (ref_obj_id == tmp_obj_id || root_obj_id == tmp_obj_id) {
            // skip
          } else if (has_exist_in_array(objs, {static_cast<uint64_t> (tmp_obj_id), static_cast<share::schema::ObObjectType> (tmp_type)})) {
            // dedpulicate
          } else if (OB_FAIL(objs.push_back({static_cast<uint64_t> (tmp_obj_id), static_cast<share::schema::ObObjectType> (tmp_type)}))) {
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
          ret = OB_SUCC(ret) ? OB_ERR_UNEXPECTED : ret;
          LOG_WARN("read dependency info failed", K(ret));
        }
      }
    }
  }
  bool is_overflow = false;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(check_stack_overflow(is_overflow))) {
  } else if (is_overflow) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("too deep recusive", K(ret));
  } else {
    for (int64_t i = init_count; OB_SUCC(ret) && i < objs.count(); ++i) {
      if (OB_FAIL(collect_all_dep_objs_inner(root_obj_id, objs.at(i).first, sql_proxy, objs))) {
      }
    }
  }
  return ret;
}

int ObDependencyInfo::collect_all_dep_objs(uint64_t ref_obj_id,
                                           ObObjectType ref_obj_type,
                                           common::ObISQLClient &sql_proxy,
                                           common::ObIArray<CriticalDepInfo> &objs)
{
  int ret = OB_SUCCESS;
  ObArray<std::pair<uint64_t, int64_t>> ref_obj_infos;
  OZ (ref_obj_infos.push_back({ref_obj_id, static_cast<int64_t>(ref_obj_type)}));
  OZ (collect_all_dep_objs(ref_obj_infos, sql_proxy, objs));
  return ret;
}

int ObDependencyInfo::collect_all_dep_objs(
    const common::ObIArray<std::pair<uint64_t, int64_t>>& ref_obj_infos,
    common::ObISQLClient &sql_proxy,
    common::ObIArray<CriticalDepInfo> &objs)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;

  const int64_t init_count = objs.count();
  if (OB_SUCC(ret) && !ref_obj_infos.empty()) {
    HEAP_VAR(common::ObMySQLProxy::MySQLResult, res) {
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(sql.assign_fmt(
          "SELECT dep_obj_id, dep_obj_type, schema_version FROM %s "
          "WHERE (ref_obj_id, ref_obj_type) IN (",
          OB_ALL_DEPENDENCY_TNAME))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < ref_obj_infos.count(); i++) {
        uint64_t ref_obj_id = ref_obj_infos.at(i).first;
        int64_t ref_obj_type = ref_obj_infos.at(i).second;
        if (OB_FAIL(sql.append_fmt("(%lu, %ld)", ref_obj_id, ref_obj_type))) {
        } else if (OB_FAIL(sql.append_fmt(i < ref_obj_infos.count() - 1 ? ", " : ")"))) {
        }
      }
      common::sqlclient::ObMySQLResult *result = nullptr;
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(sql_proxy.read(res, sql.ptr()))) {
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is null", K(ret));
      } else {
        while (OB_SUCC(result->next())) {
          int64_t tmp_obj_id = OB_INVALID_ID;
          int64_t tmp_type = static_cast<int64_t>(share::schema::ObObjectType::INVALID);
          int64_t tmp_schema_version = OB_INVALID_VERSION;
          EXTRACT_INT_FIELD_MYSQL(*result, "dep_obj_id", tmp_obj_id, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "dep_obj_type", tmp_type, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "schema_version", tmp_schema_version, int64_t);
          CriticalDepInfo tmp_tuple{static_cast<uint64_t>(tmp_obj_id), tmp_type,
                                    tmp_schema_version};
          if (OB_FAIL(ret)) {
          } else if (tmp_type <= static_cast<int64_t>(share::schema::ObObjectType::INVALID)
                     || tmp_type >= static_cast<int64_t>(share::schema::ObObjectType::MAX_TYPE)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get wrong obj type", K(ret));
          } else if (has_exist_in_array(objs, tmp_tuple)) {
            // dedpulicate
          } else if (OB_FAIL(objs.push_back(tmp_tuple))) {
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
          ret = OB_SUCC(ret) ? OB_ERR_UNEXPECTED : ret;
          LOG_WARN("read dependency info failed", K(ret));
        }
      }
    }
  }

  if (OB_SUCC(ret) && init_count < objs.count()) {
    ObArray<std::pair<uint64_t, int64_t>> new_ref_obj_infos;
    for (int64_t i = init_count; OB_SUCC(ret) && i < objs.count(); i++) {
      uint64_t ref_obj_id = objs.at(i).element<0>();
      int64_t ref_obj_type = objs.at(i).element<1>();
      OZ (new_ref_obj_infos.push_back({ref_obj_id, ref_obj_type}));
    }
    OZ (SMART_CALL(collect_all_dep_objs(new_ref_obj_infos, sql_proxy, objs)),
        init_count, new_ref_obj_infos, objs);
  }
  return ret;
}

int ObDependencyInfo::batch_invalidate_dependents(const common::ObIArray<CriticalDepInfo> &objs,
                                                  common::ObMySQLTransaction &trans,
                                                  uint64_t ref_obj_id)
{
  int ret = OB_SUCCESS;
  if (objs.empty()) {
    // no dependents
  } else {
    share::ObDMLSqlSplicer dml;

    ObString err_info_text("has a non-existing reference object");
    for (int64_t i = 0; OB_SUCC(ret) && i < objs.count(); i++) {
      ObObjectType obj_type = static_cast<ObObjectType>(objs.at(i).element<1>());
      if (ObObjectType::PACKAGE != obj_type
          && ObObjectType::PACKAGE_BODY != obj_type
          && ObObjectType::FUNCTION != obj_type
          && ObObjectType::PROCEDURE != obj_type
          && ObObjectType::TRIGGER != obj_type) {
        // types other than the above have different strategies for implementing INVALID status
        LOG_DEBUG("omitted object", K(i), K(objs.at(i)));
      } else if (OB_FAIL(dml.add_pk_column("obj_id", objs.at(i).element<0>()))
          || OB_FAIL(dml.add_pk_column("obj_type", objs.at(i).element<1>()))
          || OB_FAIL(dml.add_pk_column("obj_seq", 0))
          || OB_FAIL(dml.add_column("line", 0))
          || OB_FAIL(dml.add_column("position", 0))
          || OB_FAIL(dml.add_column("text_length", err_info_text.length()))
          || OB_FAIL(dml.add_column("property", 0))
          || OB_FAIL(dml.add_column("error_number", 0))
          || OB_FAIL(dml.add_column("text", ObHexEscapeSqlStr(err_info_text)))
          || OB_FAIL(dml.add_column("schema_version", objs.at(i).element<2>()))
          || OB_FAIL(dml.add_gmt_create())
          || OB_FAIL(dml.add_gmt_modified())
          || OB_FAIL(dml.finish_row())) {
        LOG_WARN("add column failed", K(ret));
      }
    }

    int64_t affected_rows = 0;
    ObSqlString sql;
    if (OB_FAIL(ret) || dml.get_row_count() <= 0) {
    } else if (OB_FAIL(dml.splice_batch_insert_update_sql(OB_ALL_ERROR_TNAME, sql))) {
    } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
    } else {
      // insert or update __all_error succeed!
    }
  }
  return ret;
}

// modify_dep_obj_status / cascading_modify_obj_status / modify_all_obj_status

void ObDependencyInfo::reset()
{
  dep_obj_id_ = OB_INVALID_ID;
  dep_obj_type_ = ObObjectType::MAX_TYPE;
  order_ = 0;
  dep_timestamp_ = -1;
  ref_obj_id_ = OB_INVALID_ID;
  ref_obj_type_ = ObObjectType::MAX_TYPE;
  ref_timestamp_ = -1;
  dep_obj_owner_id_ = OB_INVALID_ID;
  property_ = 0;
  reset_string(dep_attrs_);
  reset_string(dep_reason_);
  reset_string(ref_obj_name_);
  schema_version_ = OB_INVALID_VERSION;
}

int64_t ObDependencyInfo::get_convert_size() const
{
  int64_t len = 0;
  len += static_cast<int64_t>(sizeof(ObDependencyInfo));
  len += dep_attrs_.length() + 1;
  len += dep_reason_.length() + 1;
  len += ref_obj_name_.length() + 1;
  return len;
}

OB_DEF_SERIALIZE(ObDependencyInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              dep_obj_id_,
              dep_obj_type_,
              order_,
              dep_timestamp_,
              ref_obj_id_,
              ref_obj_type_,
              ref_timestamp_,
              dep_obj_owner_id_,
              property_,
              dep_attrs_,
              dep_reason_,
              ref_obj_name_,
              schema_version_);
  return ret;
}

OB_DEF_DESERIALIZE(ObDependencyInfo)
{
  int ret = OB_SUCCESS;
  reset();
  LST_DO_CODE(OB_UNIS_DECODE,
              dep_obj_id_,
              dep_obj_type_,
              order_,
              dep_timestamp_,
              ref_obj_id_,
              ref_obj_type_,
              ref_timestamp_,
              dep_obj_owner_id_,
              property_,
              dep_attrs_,
              dep_reason_,
              ref_obj_name_,
              schema_version_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDependencyInfo)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              dep_obj_id_,
              dep_obj_type_,
              order_,
              dep_timestamp_,
              ref_obj_id_,
              ref_obj_type_,
              ref_timestamp_,
              dep_obj_owner_id_,
              property_,
              dep_attrs_,
              dep_reason_,
              ref_obj_name_,
              schema_version_);
  return len;
}

int64_t ObReferenceObjTable::ObDependencyObjKey::hash() const
{
  int64_t hash_val = 0;
  hash_val = murmurhash(&dep_obj_id_, sizeof(int64_t), hash_val);
  hash_val = murmurhash(&dep_db_id_, sizeof(int64_t), hash_val);
  hash_val = murmurhash(&dep_obj_type_, sizeof(ObObjectType), hash_val);
  return hash_val;
}

ObReferenceObjTable::ObDependencyObjKey &ObReferenceObjTable::ObDependencyObjKey::operator=(
    const ObDependencyObjKey &other)
{
  if (this != &other) {
    dep_obj_id_ = other.dep_obj_id_;
    dep_db_id_ = other.dep_db_id_;
    dep_obj_type_ = other.dep_obj_type_;
  }
  return *this;
}

int ObReferenceObjTable::ObDependencyObjKey::assign(const ObDependencyObjKey &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    dep_obj_id_ = other.dep_obj_id_;
    dep_db_id_ = other.dep_db_id_;
    dep_obj_type_ = other.dep_obj_type_;
  }
  return ret;
}

bool ObReferenceObjTable::ObDependencyObjKey::operator==(
     const ObReferenceObjTable::ObDependencyObjKey &other) const
{
  return dep_obj_id_ == other.dep_obj_id_ &&
    dep_db_id_ == other.dep_db_id_ &&
    dep_obj_type_ == other.dep_obj_type_;
}

OB_SERIALIZE_MEMBER(ObReferenceObjTable::ObDependencyObjKey,
                    dep_obj_id_,
                    dep_db_id_,
                    dep_obj_type_);

ObReferenceObjTable::ObDependencyObjItem& ObReferenceObjTable::ObDependencyObjItem::operator=(
                     const ObReferenceObjTable::ObDependencyObjItem &other)
{
  if (this != &other) {
    reset();
    int &ret = error_ret_;
    ref_obj_op_ = other.ref_obj_op_;
    max_dependency_version_ = other.max_dependency_version_;
    max_ref_obj_schema_version_ = other.max_ref_obj_schema_version_;
    dep_obj_schema_version_ = other.dep_obj_schema_version_;
    if (OB_FAIL(ref_obj_versions_.assign(other.ref_obj_versions_))) {
    }
    error_ret_ = ret;
  }
  return *this;
}

int ObReferenceObjTable::ObDependencyObjItem::assign(
    const ObReferenceObjTable::ObDependencyObjItem &other)
{
  int ret = OB_SUCCESS;
  this->operator=(other);
  ret = this->error_ret_;
  return ret;
}

void ObReferenceObjTable::ObDependencyObjItem::reset()
{
  error_ret_ = OB_SUCCESS;
  ref_obj_op_ = INVALID_OP;
  max_dependency_version_ = OB_INVALID_VERSION;
  max_ref_obj_schema_version_ = OB_INVALID_VERSION;
  dep_obj_schema_version_ = OB_INVALID_VERSION;
  ref_obj_versions_.reset();
}

int ObReferenceObjTable::ObDependencyObjItem::add_ref_obj_version(const ObSchemaObjVersion &ref_obj)
{
  int ret = OB_SUCCESS;
  ObSchemaRefObjOp op = INVALID_OP;
  bool is_found = false;
  for (int64_t i = 0; OB_SUCC(ret) && !is_found && i < ref_obj_versions_.count(); ++i) {
    const ObSchemaObjVersion &obj_version = ref_obj_versions_.at(i);
    if (obj_version.get_object_id() == ref_obj.get_object_id()
        && obj_version.object_type_ == ref_obj.object_type_) {
      is_found = true;
    }
  }
  if (OB_SUCC(ret) && !is_found) {
    if (INVALID_OP == ref_obj_op_) {
      if (OB_INVALID_VERSION == max_dependency_version_) {
        ref_obj_op_ = INSERT_OP;
      } else if (max_dependency_version_ < ref_obj.version_) {
        ref_obj_op_ = UPDATE_OP;
      }
    }
    if (max_ref_obj_schema_version_ < ref_obj.version_) {
      max_ref_obj_schema_version_ = ref_obj.version_;
    }
    ret = ref_obj_versions_.push_back(ref_obj);
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObReferenceObjTable::ObDependencyObjItem,
                    ref_obj_op_,
                    max_dependency_version_,
                    max_ref_obj_schema_version_,
                    dep_obj_schema_version_,
                    ref_obj_versions_);

void ObReferenceObjTable::DependencyObjKeyItemPair::reset()
{
  dep_obj_key_.reset();
  dep_obj_item_.reset();
}



int ObReferenceObjTable::DependencyObjKeyItemPair::assign(
    const ObReferenceObjTable::DependencyObjKeyItemPair &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    if (OB_FAIL(dep_obj_key_.assign(other.dep_obj_key_))) {
    } else if (OB_FAIL(dep_obj_item_.assign(other.dep_obj_item_))) {
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObReferenceObjTable::DependencyObjKeyItemPair,
                    dep_obj_key_,
                    dep_obj_item_);

int ObReferenceObjTable::ObGetDependencyObjOp::operator()(
     hash::HashMapPair<ObDependencyObjKey, ObDependencyObjItem *> &entry)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(insert_dep_objs_) || OB_ISNULL(update_dep_objs_) || OB_ISNULL(delete_dep_objs_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_ISNULL(entry.second)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dependency object item is null", KP(entry.second), K(ret));
  } else if (is_sys_view(entry.first.dep_obj_id_) || is_sys_table(entry.first.dep_obj_id_)) {
    // do nothing
  } else if (OB_INVALID_ID == entry.first.dep_obj_id_) {
    // do nothing
  } else {
    ObSchemaRefObjOp op = entry.second->get_ref_obj_op();
    ObReferenceObjTable::DependencyObjKeyItemPair key_item(entry.first, *entry.second);
    switch (op) {
    case INSERT_OP:
      if (OB_FAIL(insert_dep_objs_->push_back(key_item))) {
      }
      break;
    case DELETE_OP:
      if (OB_FAIL(delete_dep_objs_->push_back(key_item))) {
      }
      break;
    case UPDATE_OP:
      if (OB_FAIL(update_dep_objs_->push_back(key_item))) {
      }
      break;
    default:
      break;
    }
    if (ret != OB_SUCCESS) {
      callback_ret_ = ret;
    }
  }
  return ret;
}

void ObReferenceObjTable::reset()
{
  int ret = OB_SUCCESS;
  auto free_func = [](common::hash::HashMapPair<ObDependencyObjKey, ObDependencyObjItem*> &entry) -> int {
    int ret = OB_SUCCESS;
    if (OB_NOT_NULL(entry.second)) {
      entry.second->~ObDependencyObjItem();
      entry.second = nullptr;
    }
    return ret;
  };
  if (!ref_obj_version_table_.created()) {
    // do nothing
  } else if (OB_FAIL(ref_obj_version_table_.foreach_refactored(free_func))) {
  }
  inited_ = false;
  ref_obj_version_table_.destroy();
}

int ObReferenceObjTable::fill_rowkey_pairs(
    const ObDependencyObjKey &dep_obj_key,
    share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(dml.add_pk_column("dep_obj_id", ObSchemaUtils::get_extract_schema_id(
                 dep_obj_key.dep_obj_id_)))
      || OB_FAIL(dml.add_pk_column("dep_obj_type", static_cast<uint64_t>(
                 dep_obj_key.dep_obj_type_)))) {
    LOG_WARN("add column failed", K(ret));
  } else if (OB_FAIL(dml.finish_row())) {
  }
  return ret;
}

int ObReferenceObjTable::batch_execute_delete_obj_dependency(const ObReferenceObjTable::DependencyObjKeyItemPairs &dep_objs,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  {
    share::ObDMLSqlSplicer dml;
    ObSqlString sql;
    int64_t affected_rows = 0;
    for (int64_t i = 0 ; OB_SUCC(ret) && i < dep_objs.count(); ++i) {
      ObSArray<ObDependencyInfo> dep_infos;
      const ObDependencyObjKey &dep_obj_key = dep_objs.at(i).dep_obj_key_;
      if (!dep_obj_key.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("illegal schema version or dependency obj key", K(ret), K(dep_obj_key));
      } else if (OB_FAIL(fill_rowkey_pairs(dep_obj_key, dml))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(dml.splice_batch_delete_sql(OB_ALL_DEPENDENCY_TNAME, sql))) {
    } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
    } else {
    }
  }
  return ret;
}

int ObReferenceObjTable::get_or_add_def_obj_item(const uint64_t dep_obj_id,
                                                 const uint64_t dep_db_id,
                                                 const ObObjectType dep_obj_type,
                                                 ObDependencyObjItem *&dep_obj_item,
                                                 common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  dep_obj_item = nullptr;
  if (!inited_) {
    if (OB_FAIL(ref_obj_version_table_.create(32, "HashBucRefObj"))) {
    } else {
      inited_ = true;
    }
  }
  if (OB_SUCC(ret)) {
    char *buf = nullptr;
    ObDependencyObjKey ref_obj_key(dep_obj_id, dep_db_id, dep_obj_type);
    if (OB_FAIL(ref_obj_version_table_.get_refactored(ref_obj_key, dep_obj_item))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        OV (OB_NOT_NULL(buf = static_cast<char *>(allocator.alloc(sizeof(ObDependencyObjItem)))),
            OB_ALLOCATE_MEMORY_FAILED,
            K(sizeof(ObDependencyObjItem)));
        OX (dep_obj_item = new(buf) ObDependencyObjItem);
        OZ (ref_obj_version_table_.set_refactored(ref_obj_key, dep_obj_item));
      } else {
        LOG_WARN("failed to get dep obj item", K(ret));
      }
    }
  }
  return ret;
}

int ObReferenceObjTable::add_ref_obj_version(const uint64_t dep_obj_id,
                                             const uint64_t dep_db_id,
                                             const ObObjectType dep_obj_type,
                                             const ObSchemaObjVersion &ref_obj_version,
                                             common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObDependencyObjItem *dep_obj_item = nullptr;
  OZ (get_or_add_def_obj_item(dep_obj_id, dep_db_id, dep_obj_type, dep_obj_item, allocator));
  CK (OB_NOT_NULL(dep_obj_item));
  OZ (dep_obj_item->add_ref_obj_version(ref_obj_version));
  return ret;
}

int ObReferenceObjTable::get_dep_obj_item(const uint64_t dep_obj_id,
                                          const uint64_t dep_db_id,
                                          const ObObjectType dep_obj_type,
                                          ObDependencyObjItem *&dep_obj_item)
{
  int ret = OB_SUCCESS;
  ObDependencyObjKey dep_obj_key(dep_obj_id, dep_db_id, dep_obj_type);
  if (!is_inited()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ref_obj_version_table_ not inited", K(ret));
  } else if (OB_FAIL(ref_obj_version_table_.get_refactored(dep_obj_key, dep_obj_item))) {
  }
  return ret;
}

int ObReferenceObjTable::set_obj_schema_version(const uint64_t dep_obj_id,
                                                const uint64_t dep_db_id,
                                                const ObObjectType dep_obj_type,
                                                const int64_t max_dependency_version,
                                                const int64_t dep_obj_schema_version,
                                                common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObDependencyObjItem *dep_obj_item = nullptr;
  OZ (get_or_add_def_obj_item(dep_obj_id, dep_db_id, dep_obj_type, dep_obj_item, allocator));
  CK (OB_NOT_NULL(dep_obj_item));
  OX (dep_obj_item->set_max_dependency_version(max_dependency_version));
  OX (dep_obj_item->set_dep_obj_schema_version(dep_obj_schema_version));
  return ret;
}

int ObReferenceObjTable::set_ref_obj_op(const uint64_t dep_obj_id,
                                        const uint64_t dep_db_id,
                                        const ObObjectType dep_obj_type,
                                        const ObSchemaRefObjOp ref_obj_op,
                                        common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObDependencyObjItem *dep_obj_item = nullptr;
  OZ (get_or_add_def_obj_item(dep_obj_id, dep_db_id, dep_obj_type, dep_obj_item, allocator));
  CK (OB_NOT_NULL(dep_obj_item));
  OX (dep_obj_item->set_ref_obj_op(ref_obj_op));
  return ret;
}

// process_reference_obj_table relocated to free fn in sql/executor/ob_maintain_dependency_info_task
int ObDependencyInfo::insert_dependency_infos(common::ObMySQLTransaction &trans,
                                           ObIArray<ObDependencyInfo> &dep_infos,
                                           uint64_t dep_obj_id,
                                           uint64_t schema_version, uint64_t owner_id)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_ID == owner_id
   || OB_INVALID_ID == dep_obj_id
   || OB_INVALID_SCHEMA_VERSION == schema_version) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("illegal schema version or owner id", K(ret), K(schema_version),
                                                   K(owner_id), K(dep_obj_id));
  } else {
    for (int64_t i = 0 ; OB_SUCC(ret) && i < dep_infos.count(); ++i) {
      ObDependencyInfo & dep = dep_infos.at(i);

      dep.set_dep_obj_id(dep_obj_id);
      dep.set_dep_obj_owner_id(owner_id);
      dep.set_schema_version(schema_version);
      OZ (dep.insert_schema_object_dependency(trans));
    }
  }
  return ret;
}

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
