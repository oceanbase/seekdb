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
 #include "rootserver/parallel_ddl/ob_create_view_helper.h"
 #include "rootserver/ob_table_creator.h"
 #include "sql/resolver/ddl/ob_create_view_resolver.h"
 #include "share/schema/ob_multi_version_schema_service.h"
 #include "share/schema/ob_table_sql_service.h"
 #include "share/schema/ob_trigger_sql_service.h"
 #include "share/schema/ob_priv_sql_service.h"
 #include "rootserver/ddl_task/ob_ddl_scheduler.h"
 #include "storage/compaction/ob_compaction_schedule_util.h"
 #include "rootserver/ddl_task/ob_sys_ddl_util.h" // for ObSysDDLSchedulerUtil
 #include "share/ob_rpc_struct.h"
 using namespace oceanbase::lib;
 using namespace oceanbase::common;
 using namespace oceanbase::share;
 using namespace oceanbase::share::schema;
 using namespace oceanbase::rootserver;

ObCreateViewHelper::ObCreateViewHelper(
    share::schema::ObMultiVersionSchemaService *schema_service,
    const obcall::ObCreateTableArg &arg_,
    obcall::ObCreateTableRes &res,
    ObDDLSQLTransaction *external_trans,
    bool enable_ddl_parallel)
  : ObDDLHelper(schema_service, "[parallel create view]", external_trans, enable_ddl_parallel),
    arg_(arg_),
    res_(res),
    new_view_schema_(nullptr),
    ddl_stmt_str_(),
    database_schema_(nullptr),
    orig_table_id_(OB_INVALID_ID),
    orig_table_schema_(nullptr),
    dep_objs_(),
    dep_views_(),
    trigger_infos_(),
    obj_privs_(),
    raw_obj_privs_()
{}

ObCreateViewHelper::~ObCreateViewHelper()
{
}


 int ObCreateViewHelper::init_()
 {
   int ret = OB_SUCCESS;
   if (OB_FAIL(check_inner_stat_())) {
   } else if (OB_UNLIKELY(USER_VIEW != arg_.schema_.get_table_type())) {
     ret = OB_NOT_SUPPORTED;
     LOG_WARN("not support table type", KR(ret), K(arg_.schema_.get_table_type()));
   } else if (OB_UNLIKELY(OB_INVALID_ID != arg_.schema_.get_table_id())) {
     ret = OB_NOT_SUPPORTED;
     LOG_WARN("create view with table_id in 4.x is not supported",
              KR(ret), "table_id", arg_.schema_.get_table_id());
   } else if (OB_UNLIKELY(OB_INVALID_ID != arg_.schema_.get_tablespace_id())) {
     ret = OB_NOT_SUPPORTED;
     LOG_WARN("create view with tablespace_id in 4.x is not supported",
              KR(ret), "tablespace_id", arg_.schema_.get_tablespace_id());
   } else if (OB_UNLIKELY(PARTITION_LEVEL_ZERO != arg_.schema_.get_part_level())) {
     ret = OB_NOT_SUPPORTED;
     LOG_WARN("create view with partition in 4.x is not supported",
              KR(ret), K(arg_.schema_.get_part_level()));
   }
   return ret;
 }

 int ObCreateViewHelper::lock_objects_()
 {
   int ret = OB_SUCCESS;
   DEBUG_SYNC(BEFORE_PARALLEL_DDL_LOCK);
   if (OB_FAIL(check_inner_stat_())) {
   } else if (OB_FAIL(lock_and_check_database_())) {
   } else if (OB_FAIL(lock_and_check_view_name_())) {
   } else if (OB_FAIL(lock_object_id_())) {
   } else if (OB_FAIL(check_parallel_ddl_conflict_())) {
   }
   DEBUG_SYNC(AFTER_PARALLEL_DDL_LOCK);
   RS_TRACE(lock_objects);
   return ret;
 }

 int ObCreateViewHelper::lock_and_check_database_()
 {
   int ret = OB_SUCCESS;
   const ObString &database_name = arg_.db_name_;
   uint64_t database_id = OB_INVALID_ID;
   if (OB_FAIL(check_inner_stat_())) {
   } else if (OB_FAIL(add_lock_object_by_database_name_(database_name, transaction::tablelock::SHARE))) {
   } else if (OB_FAIL(lock_databases_by_name_())) {
   } else if (OB_FAIL(check_database_legitimacy_(database_name, database_id))) {
   } else if (OB_UNLIKELY(database_id != arg_.schema_.get_database_id())) {
     ret = OB_ERR_PARALLEL_DDL_CONFLICT;
     LOG_WARN("database_id not consistent", KR(ret), K(database_id), K(arg_.schema_.get_database_id()));
   } else {
     (void) const_cast<ObTableSchema&>(arg_.schema_).set_database_id(database_id);
   }
   return ret;
 }

 /* check view name confilict
 -- !arg_.if_not_exist
     view name should not exist
 -- arg_.if_not_exist && arg_.is_alter_view
     view name must exist and be a view
 -- arg_.if_not_exist && !arg_.is_alter_view
     view name could exist or not
     if view name exist, it must be a view

-- mysql mode
   get mock table id with name
   get table id with name
*/

int ObCreateViewHelper::lock_and_check_view_name_()
{
  int ret = OB_SUCCESS;
  const ObString &database_name = arg_.db_name_;
  const ObString &table_name = arg_.schema_.get_table_name();
  const uint64_t database_id = arg_.schema_.get_database_id();
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_FAIL(add_lock_object_by_name_(database_name, table_name, share::schema::TABLE_SCHEMA,
             transaction::tablelock::EXCLUSIVE))) {
  } else if (OB_FAIL(lock_existed_objects_by_name_())) {
  }
  const ObTableSchema* table_schema = nullptr;
  ObTableType table_type = MAX_TABLE_TYPE;
  int64_t schema_version = OB_INVALID_VERSION;
  const uint64_t session_id = arg_.schema_.get_session_id();
  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    uint64_t mock_table_id = OB_INVALID_ID;
    if (OB_FAIL(schema_guard_wrapper_.get_mock_fk_parent_table_id(database_id,
                       table_name, mock_table_id))) {
    } else if (OB_UNLIKELY(OB_INVALID_ID != mock_table_id)) {
      if (arg_.is_alter_view_) {
        ret = OB_ERR_WRONG_OBJECT;
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_ERR_WRONG_OBJECT,
            helper.convert(database_name),
            helper.convert(table_name), "VIEW");
        LOG_WARN("table exist", KR(ret), K(database_id), K(table_name));
      } else {
        ret = OB_ERR_TABLE_EXIST;
        LOG_USER_ERROR(OB_ERR_TABLE_EXIST, arg_.schema_.get_table_name_str().length(),
                   arg_.schema_.get_table_name_str().ptr());
        LOG_WARN("mock table exist", KR(ret), K(database_id), K(session_id), K(table_name),
                                K(mock_table_id), K(schema_version), K(arg_.if_not_exist_));
      }
    } else if (OB_FAIL(schema_guard_wrapper_.get_table_id(database_id, session_id, table_name,
                                                  orig_table_id_, table_type, schema_version))) {
    } else if (OB_INVALID_ID == orig_table_id_) {
      // view not exist
      // alter view asks for existed view
      if (arg_.is_alter_view_) {
        ret = OB_TABLE_NOT_EXIST;
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_TABLE_NOT_EXIST,
                       helper.convert(database_name),
                       helper.convert(table_name));
      }
    } else {
      // view should not exist when create view
      if (!arg_.if_not_exist_) {
        ret = OB_ERR_TABLE_EXIST;
        LOG_USER_ERROR(OB_ERR_TABLE_EXIST, arg_.schema_.get_table_name_str().length(),
                   arg_.schema_.get_table_name_str().ptr());
        LOG_WARN("table exist", KR(ret), K(database_id), K(session_id), K(table_name),
                              K_(orig_table_id), K(schema_version), K(arg_.if_not_exist_));
      // create or replace / alter view need to check schema type is USER/SYSTEM VIEW
      } else if (USER_VIEW == table_type
                 || (GCONF.enable_sys_table_ddl && SYSTEM_VIEW == table_type)) {
        // do nothing
      } else if (SYSTEM_VIEW == table_type) {
        ret = OB_OP_NOT_ALLOW;
        LOG_WARN("not allowed to replace sys view when enable_sys_table_ddl is false", KR(ret), K(table_type));
        LOG_USER_ERROR(OB_OP_NOT_ALLOW, "replace sys view when enable_sys_table_ddl is false");
      } else {
        ret = OB_ERR_WRONG_OBJECT;
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_ERR_WRONG_OBJECT,
                       helper.convert(database_name),
                       helper.convert(table_name), "VIEW");
        LOG_WARN("table exist", KR(ret), K(database_id), K(table_name));
      }
    }
  }
  return ret;
}

int ObCreateViewHelper::lock_object_id_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_FAIL(add_lock_object_by_id_(arg_.schema_.get_database_id(),
             share::schema::DATABASE_SCHEMA, transaction::tablelock::SHARE))) {
  } else if (OB_INVALID_ID != orig_table_id_
             && OB_FAIL(add_lock_object_by_id_(orig_table_id_, VIEW_SCHEMA, transaction::tablelock::EXCLUSIVE))) {
    LOG_WARN("fail to add lock object", KR(ret));
  } else if (OB_FAIL(lock_existed_objects_by_id_())) {
  } else if (OB_INVALID_ID != orig_table_id_) {
    if (OB_FAIL(schema_guard_wrapper_.get_table_schema(orig_table_id_, orig_table_schema_))) {
    } else if (OB_ISNULL(orig_table_schema_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("orig table schema is null", KR(ret));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && (i < arg_.dep_infos_.count()); ++i) {
    const ObDependencyInfo &dep = arg_.dep_infos_.at(i);
    ObSchemaType schema_type = transfer_obj_type_to_schema_type_for_dep_(dep.get_ref_obj_type());
    if (OB_UNLIKELY(OB_MAX_SCHEMA == schema_type)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid obj type", KR(ret), K(dep.get_ref_obj_type()));
    } else if (OB_FAIL(add_lock_object_by_id_(dep.get_ref_obj_id(), schema_type, transaction::tablelock::SHARE))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && (i < arg_.based_schema_object_infos_.count()); ++i) {
    const ObBasedSchemaObjectInfo &info = arg_.based_schema_object_infos_.at(i);
    if (is_inner_pl_udt_id(info.schema_id_) || is_inner_pl_object_id(info.schema_id_)) {
      // do nothing
    } else if (OB_FAIL(add_lock_object_by_id_(info.schema_id_,
                                              info.schema_type_,
                                              transaction::tablelock::SHARE))) {
    }
  }
  ObArray<std::pair<uint64_t, share::schema::ObObjectType>> dep_objs_before_lock;
  if (OB_FAIL(ret)) {
  } else if (OB_NOT_NULL(orig_table_schema_)) {
    if (OB_FAIL(ObDependencyInfo::collect_all_dep_objs(orig_table_schema_->get_table_id(),
                                                       *sql_proxy_, dep_objs_before_lock))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < dep_objs_before_lock.count(); ++i) {
        ObSchemaType schema_type = transfer_obj_type_to_schema_type_for_dep_(dep_objs_before_lock.at(i).second);
        if (ObObjectType::VIEW == dep_objs_before_lock.at(i).second) {
          if (OB_FAIL(add_lock_object_by_id_(dep_objs_before_lock.at(i).first,
                                             VIEW_SCHEMA, transaction::tablelock::EXCLUSIVE))) {
          }
        }
      }
    }
    const ObIArray<uint64_t> &trigger_list = orig_table_schema_->get_trigger_list();
    for (int64_t i = 0; OB_SUCC(ret) && i < trigger_list.count(); ++i) {
      if (OB_FAIL(add_lock_object_by_id_(trigger_list.at(i), TRIGGER_SCHEMA, transaction::tablelock::EXCLUSIVE))) {
      }
    }
  }

  if (FAILEDx(add_lock_table_udt_id_(arg_.schema_))) {
    LOG_WARN("fail to add lock table udt id", KR(ret));
  }

  if (FAILEDx(lock_existed_objects_by_id_())) {
    LOG_WARN("fail to lock objects by id", KR(ret));
  } else if (OB_NOT_NULL(orig_table_schema_)) {
    if (OB_FAIL(ObDependencyInfo::collect_all_dep_objs(orig_table_schema_->get_table_id(), *sql_proxy_, dep_objs_))) {
    } else if (dep_objs_.count() != dep_objs_before_lock.count()) {
      ret = OB_ERR_PARALLEL_DDL_CONFLICT;
      LOG_WARN("dep objs count not consistent", KR(ret), K(dep_objs_.count()), K(dep_objs_before_lock.count()));
    } else {
      lib::ob_sort(dep_objs_before_lock.begin(), dep_objs_before_lock.end(), dep_compare_func_);
      lib::ob_sort(dep_objs_.begin(), dep_objs_.end(), dep_compare_func_);
      for (int64_t i = 0; OB_SUCC(ret) && i < dep_objs_.count(); ++i) {
        if (dep_objs_before_lock.at(i).first != dep_objs_.at(i).first) {
          ret = OB_ERR_PARALLEL_DDL_CONFLICT;
          LOG_WARN("dep obj in double check not in exist list", KR(ret),
                   K(dep_objs_.at(i).first), K(dep_objs_.at(i).second),
                   K(dep_objs_before_lock.at(i).first), K(dep_objs_before_lock.at(i).second));
        } else if (dep_objs_before_lock.at(i).second != dep_objs_.at(i).second) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("different obj type with same obj id is unexpected", KR(ret),
                   K(dep_objs_.at(i).first), K(dep_objs_.at(i).second),
                   K(dep_objs_before_lock.at(i).first), K(dep_objs_before_lock.at(i).second));
        }
      }
    }
  }
  return ret;
}

int ObCreateViewHelper::check_parallel_ddl_conflict_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_FAIL(ObDDLHelper::check_parallel_ddl_conflict_(arg_.based_schema_object_infos_))) {
  } else if (OB_FAIL(check_table_udt_exist_(arg_.schema_))) {
  } else {
    bool exist = true;
    ObArray<uint64_t> table_ids;
    ObArray<uint64_t> routine_ids;
    ObArray<uint64_t> package_ids;
    for (int64_t i = 0; OB_SUCC(ret) && (i < arg_.dep_infos_.count()); ++i) {
      const ObDependencyInfo &dep = arg_.dep_infos_.at(i);
      if (is_inner_pl_object_id(dep.get_ref_obj_id())
          || is_inner_pl_udt_id(dep.get_ref_obj_id())) {
        // do nothing
      } else {
        switch (dep.get_ref_obj_type()) {
          case ObObjectType::TABLE:
          case ObObjectType::VIEW:
            if (OB_FAIL(table_ids.push_back(dep.get_ref_obj_id()))) {
            }
            break;
          case ObObjectType::PROCEDURE:
          case ObObjectType::FUNCTION:
            if (OB_FAIL(routine_ids.push_back(dep.get_ref_obj_id()))) {
            }
            break;
          case ObObjectType::PACKAGE:
          case ObObjectType::PACKAGE_BODY:
            if (OB_FAIL(package_ids.push_back(dep.get_ref_obj_id()))) {
            }
            break;
          default:
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("unexpected obj type", KR(ret), K(dep));
            break;
        }
      }
    }
  #ifndef CHECK_MAX_DEPENDENCY_VERSION
  #define CHECK_MAX_DEPENDENCY_VERSION(SCHEMA_TYPE) \
    ObArray<ObSchemaIdVersion> SCHEMA_TYPE##_schema_versions; \
    if (OB_FAIL(ret)) { \
    } else if (0 == SCHEMA_TYPE##_ids.count()) { \
    } else if (OB_FAIL(schema_guard_wrapper_.get_##SCHEMA_TYPE##_schema_versions(SCHEMA_TYPE##_ids, SCHEMA_TYPE##_schema_versions))) { \
      LOG_WARN("failed to get " #SCHEMA_TYPE " schema versions", KR(ret), K(SCHEMA_TYPE##_ids)); \
    } else if (OB_FAIL(check_max_dependency_version_(SCHEMA_TYPE##_ids, SCHEMA_TYPE##_schema_versions))) { \
      LOG_WARN("fail to check max dependency version", KR(ret), K(SCHEMA_TYPE##_ids), K(SCHEMA_TYPE##_schema_versions)); \
    }
    CHECK_MAX_DEPENDENCY_VERSION(table)
  #undef CHECK_MAX_DEPENDENCY_VERSION
  #endif
  }
  return ret;
}

int ObCreateViewHelper::check_max_dependency_version_(const common::ObIArray<uint64_t> &obj_ids,
                                                      const common::ObIArray<ObSchemaIdVersion> &versions)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(obj_ids.count() != versions.count())) {
    ret = OB_ERR_PARALLEL_DDL_CONFLICT;
    LOG_WARN("obj_ids' count not match versions' count", KR(ret), K(obj_ids.count()), K(versions.count()),
                                                         K(obj_ids), K(versions));
  } else {
    for (uint64_t i = 0; OB_SUCC(ret) && i < versions.count(); ++i) {
      if (versions.at(i).get_schema_version() > arg_.schema_.get_max_dependency_version()) {
        ret = OB_ERR_PARALLEL_DDL_CONFLICT;
        LOG_WARN("table schema version larger than max dependency version", KR(ret),
                 K(versions.at(i).get_schema_version()),
                 K(arg_.schema_.get_max_dependency_version()));
      }
    }
  }
  return ret;
}

int ObCreateViewHelper::generate_schemas_()
{
  int ret = OB_SUCCESS;
  ObIDGenerator id_generator;
  const uint64_t object_cnt = 1;
  uint64_t object_id = OB_INVALID_ID;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_FAIL(gen_object_ids_(object_cnt, id_generator))) {
  } else if (OB_FAIL(id_generator.next(object_id))) {
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator_, arg_.schema_, new_view_schema_))) {
  } else if (OB_UNLIKELY(OB_ISNULL(new_view_schema_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new view schema is null", KR(ret));
  } else {
    new_view_schema_->set_table_id(object_id);
  }
  if (FAILEDx(print_view_expanded_definition_())) {
    LOG_WARN("fail to print view expanded definition", KR(ret));
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(orig_table_schema_)) {
    const uint64_t orig_table_id = orig_table_schema_->get_table_id();
    if (OB_FAIL(schema_guard_wrapper_.get_obj_privs(orig_table_id, ObObjectType::TABLE, obj_privs_))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < obj_privs_.count(); ++i) {
        std::pair<ObRawObjPrivArray, ObRawObjPrivArray> raw_obj_priv;
        ObObjPriv &obj_priv = obj_privs_.at(i);
        if (OB_FAIL(ObPrivPacker::raw_option_obj_priv_from_pack(obj_priv.get_obj_privs(), raw_obj_priv.first))) {
        } else if (OB_FAIL(ObPrivPacker::raw_no_option_obj_priv_from_pack(obj_priv.get_obj_privs(), raw_obj_priv.second))) {
        } else if (OB_FAIL(raw_obj_privs_.push_back(raw_obj_priv))) {
        }
      }
    }
    const ObIArray<uint64_t> &trigger_list = orig_table_schema_->get_trigger_list();
    for (int64_t i = 0; OB_SUCC(ret) && i < trigger_list.count(); ++i) {
      const ObTriggerInfo* trigger_info = nullptr;
      if (OB_FAIL(schema_guard_wrapper_.get_trigger_info(trigger_list.at(i), trigger_info))) {
      } else if (OB_ISNULL(trigger_info)) {
        ret = OB_ERR_PARALLEL_DDL_CONFLICT;
        LOG_WARN("trigger info is null, may be dropped", KR(ret));
      } else if (OB_UNLIKELY(trigger_info->is_in_recyclebin())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("trigger is in recyclebin", KR(ret), KP(trigger_info));
      } else if (OB_FAIL(trigger_infos_.push_back(trigger_info))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < dep_objs_.count(); ++i) {
      if (ObObjectType::VIEW == dep_objs_.at(i).second) {
        const ObTableSchema* view_schema = nullptr;
        if (OB_FAIL(schema_guard_wrapper_.get_table_schema(dep_objs_.at(i).first, view_schema))) {
        } else if (OB_ISNULL(view_schema)) {
          ret = OB_ERR_PARALLEL_DDL_CONFLICT;
        } else if (ObObjectStatus::INVALID == view_schema->get_object_status()) {
          // do nothing
        } else if (OB_FAIL(dep_views_.push_back(view_schema))) {
        }
      }
    }
  }
  RS_TRACE(generate_schemas);
  return ret;
}

int ObCreateViewHelper::print_view_expanded_definition_()
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t buf_len = OB_MAX_VARCHAR_LENGTH;
  int64_t pos = 0;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_UNLIKELY(OB_ISNULL(new_view_schema_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new view schema is null", KR(ret));
  } else {
    if (OB_ISNULL(buf = static_cast<char*>(allocator_.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate memory", KR(ret), K(buf_len));
    } else if (OB_FAIL(schema_guard_wrapper_.get_database_schema(arg_.schema_.get_database_id(), database_schema_))) {
    } else if (OB_ISNULL(database_schema_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("database schema is null", KR(ret), K(arg_.schema_.get_database_id()));
    } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
               "CREATE%s VIEW `%s`.`%s` AS %.*s;",
               arg_.if_not_exist_ ? " OR REPLACE" : "",
               database_schema_->get_database_name(),
               new_view_schema_->get_table_name(),
               new_view_schema_->get_view_schema().get_view_definition_str().length(),
               new_view_schema_->get_view_schema().get_view_definition_str().ptr()))) {
    } else {
      ddl_stmt_str_.assign_ptr(buf, static_cast<int32_t>(pos));
    }
  }
  return ret;
}



int ObCreateViewHelper::calc_schema_version_cnt_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else {
    schema_version_cnt_ = 0;
    if (OB_NOT_NULL(orig_table_schema_)) {
      // drop trigger
      schema_version_cnt_ += trigger_infos_.count();
      // modify status
      schema_version_cnt_ += dep_views_.count();
      // drop privs
      schema_version_cnt_ += obj_privs_.count();
      // drop orig view
      schema_version_cnt_ ++;
      // restore privs
      for (int64_t i = 0; i < raw_obj_privs_.count(); ++i) {
        if (raw_obj_privs_.at(i).first.count() > 0) {
          schema_version_cnt_ ++;
        }
        if (raw_obj_privs_.at(i).second.count() > 0) {
          schema_version_cnt_ ++;
        }
      }
    }
    // create view
    schema_version_cnt_ += 1;
    // 1503
    schema_version_cnt_ ++;
  }
  return ret;
}

int ObCreateViewHelper::create_schemas_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_FAIL(create_table_())) {
  } else if (OB_FAIL(insert_schema_object_dependency_())) {
  } else if (OB_FAIL(restore_obj_privs_())) {
  } else if (OB_FAIL(handle_error_info_())) {
  } else {
    int64_t last_schema_version = OB_INVALID_VERSION;
    ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
    ObSchemaService *schema_service_impl = schema_service_->get_schema_service();
    ObSchemaVersionGenerator *tsi_generator = GET_TSI(TSISchemaVersionGenerator);
    if (OB_ISNULL(tsi_generator)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tsi generator is null", KR(ret));
    } else if (OB_ISNULL(schema_service_impl)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema service must not by null", KR(ret));
    } else if (OB_UNLIKELY(OB_ISNULL(new_view_schema_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new view schema is null", KR(ret));
    } else if (OB_FAIL(tsi_generator->get_current_version(last_schema_version))) {
    } else if (OB_UNLIKELY(last_schema_version <= 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("last schema version is invalid", KR(ret), K(last_schema_version));
    } else if (OB_FAIL(ddl_operator.insert_ori_schema_version(get_trans_(), new_view_schema_->get_table_id(), last_schema_version))) {
    }
  }
  RS_TRACE(create_schemas);
  return ret;
}

int ObCreateViewHelper::create_table_()
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = nullptr;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(schema_service_impl = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service impl is null", KR(ret));
  } else if (OB_ISNULL(new_view_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new_view_schema_ is null", KR(ret));
  } else {
    int64_t new_schema_version = OB_INVALID_VERSION;
    if (OB_FAIL(schema_service_->gen_new_schema_version(new_schema_version))) {
    } else if (FALSE_IT(new_view_schema_->set_schema_version(new_schema_version))) {
    } else if (OB_FAIL(schema_service_impl->get_table_sql_service().create_table(
                       *new_view_schema_,
                       get_trans_(),
                       &ddl_stmt_str_,
                       false /*need sync schema version*/,
                       false /*is truncate table*/))) {
    }
  }
  return ret;
}

int ObCreateViewHelper::insert_schema_object_dependency_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_UNLIKELY(OB_ISNULL(new_view_schema_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new view schema is null", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < arg_.dep_infos_.count(); ++i) {
      ObDependencyInfo dep;
      if (OB_FAIL(dep.assign(arg_.dep_infos_.at(i)))) {
      } else {
        
        dep.set_dep_obj_id(new_view_schema_->get_table_id());
        dep.set_dep_obj_owner_id(new_view_schema_->get_table_id());
        dep.set_schema_version(new_view_schema_->get_schema_version());
        if (OB_FAIL(dep.insert_schema_object_dependency(get_trans_()))) {
        }
      }
    }
  }
  return ret;
}

int ObCreateViewHelper::drop_schemas_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(orig_table_schema_)) {
    // do nothing
  } else if (OB_FAIL(drop_trigger_schemas_())) {
  } else if (OB_FAIL(modify_obj_status_())) {
  } else if (OB_FAIL(drop_obj_privs_())) {
  } else if (OB_FAIL(drop_table_())) {
  }
  RS_TRACE(drop_schemas);
  return ret;
}

int ObCreateViewHelper::modify_obj_status_()
{
  int ret = OB_SUCCESS;
  ObSchemaService* schema_service = nullptr;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(schema_service = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dep_views_.count(); ++i) {
      const ObTableSchema *view_schema = dep_views_.at(i);
      int64_t new_schema_version = OB_INVALID_VERSION;
      if (OB_ISNULL(view_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("view schema is null", KR(ret));
      } else if (OB_FAIL(schema_service_->gen_new_schema_version(new_schema_version))) {
      } else {
        ObObjectStatus new_status = ObObjectStatus::INVALID;
        const bool update_object_status_ignore_version = false;
        ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
        HEAP_VAR(ObTableSchema, new_dep_view) {
        if (OB_FAIL(new_dep_view.assign(*view_schema))) {
        } else if (OB_FAIL(ddl_operator.update_table_status(new_dep_view, new_schema_version, new_status,
                            update_object_status_ignore_version, get_trans_()))) {
        }
        } // end heap var
      }
    }
  }
  return ret;
}

int ObCreateViewHelper::drop_trigger_schemas_() {
  int ret = OB_SUCCESS;
  ObSchemaService* schema_service = nullptr;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(schema_service = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < trigger_infos_.count(); ++i) {
      const ObTriggerInfo *trigger_info = trigger_infos_.at(i);
      int64_t new_schema_version = OB_INVALID_VERSION;
      if (OB_ISNULL(trigger_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("trigger info is null", KR(ret));
      } else if (OB_FAIL(schema_service_->gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_service->get_trigger_sql_service().drop_trigger(*trigger_info,
                                                                    false /* drop to recyclebin */,
                                                                    new_schema_version,
                                                                    get_trans_(),
                                                                    nullptr /* ddl stmt str */))) {
      } else if (OB_FAIL(ObDependencyInfo::delete_schema_object_dependency(get_trans_(),
                                                                           trigger_info->get_trigger_id(),
                                                                           new_schema_version /* not used */,
                                                                           trigger_info->get_object_type()))) {
      }
      if (OB_SUCC(ret)) {
        ObErrorInfo error_info;
        if (OB_FAIL(error_info.handle_error_info(get_trans_(), trigger_info))) {
        }
      }
    }
  }
  return ret;
}

int ObCreateViewHelper::drop_obj_privs_()
{
  int ret = OB_SUCCESS;
  ObSchemaService* schema_service = nullptr;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(schema_service = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < obj_privs_.count(); ++i) {
      int64_t new_schema_version = OB_INVALID_VERSION;
      if (OB_FAIL(schema_service_->gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_service->get_priv_sql_service().delete_obj_priv(obj_privs_.at(i),
                                                                               new_schema_version, get_trans_()))) {
      }
    }
  }
  return ret;
}

int ObCreateViewHelper::handle_error_info_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (ERROR_STATUS_HAS_ERROR != arg_.error_info_.get_error_status()) {
    // do nothing
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected create view error info", KR(ret));
  }
  return ret;
}

int ObCreateViewHelper::drop_table_()
{
  int ret = OB_SUCCESS;
  ObSchemaService* schema_service = nullptr;
  int64_t new_schema_version = OB_INVALID_VERSION;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(schema_service = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret));
  } else if (OB_ISNULL(orig_table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("orig_table_schema_ is null", KR(ret));
  } else if (OB_FAIL(schema_service_->gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().drop_table(*orig_table_schema_,
                                                                        new_schema_version,
                                                                        get_trans_(),
                                                                        nullptr /* ddl_stmt_str */,
                                                                        false /* is_truncate */,
                                                                        false /* is_drop_db */,
                                                                        false /* is_force_drop_lonely_lob_aux_table */,
                                                                        nullptr /* schema_guard */,
                                                                        nullptr /* drop_table_set */))) {
  } else if (OB_FAIL(ObDependencyInfo::delete_schema_object_dependency(get_trans_(),
                                                                       orig_table_schema_->get_table_id(),
                                                                       orig_table_schema_->get_schema_version() /*not used*/,
                                                                       ObObjectType::VIEW))) {
  }
  return ret;
}

int ObCreateViewHelper::restore_obj_privs_()
{
  int ret = OB_SUCCESS;
  ObSchemaService* schema_service = nullptr;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(schema_service = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret));
  } else if (OB_ISNULL(database_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("database schema is null", KR(ret));
  } else if (OB_UNLIKELY(OB_ISNULL(new_view_schema_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("new view schema is null", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < obj_privs_.count(); ++i) {
      ObObjPriv &obj_priv = obj_privs_.at(i);
      ObTablePrivSortKey table_key(obj_priv.get_grantee_id(),
                                   database_schema_->get_database_name(),
                                   new_view_schema_->get_table_name());
      obj_priv.set_obj_id(new_view_schema_->get_table_id());
      ObObjPrivSortKey obj_priv_key = obj_priv.get_sort_key();
      const ObRawObjPrivArray &option_priv = raw_obj_privs_.at(i).first;
      const ObRawObjPrivArray &no_option_priv = raw_obj_privs_.at(i).second;
      if (option_priv.count() > 0) {
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_FAIL(schema_service_->gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_service->get_priv_sql_service().grant_table_ora_only(
          nullptr /* ddl_stmt_str */, get_trans_(), option_priv, true /* option */, obj_priv_key,
          new_schema_version, false /* is_delete */, false /* is_delete_all */))) {
        }
      }
      if (OB_SUCC(ret) && no_option_priv.count() > 0) {
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_FAIL(schema_service_->gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_service->get_priv_sql_service().grant_table_ora_only(
          nullptr /* ddl_stmt_str */, get_trans_(), no_option_priv, false /* option */,
          obj_priv_key, new_schema_version, false /* is_delete */, false /* is_delete_all */))) {
        }
      }
    }
  }
  return ret;
}

int ObCreateViewHelper::operate_schemas_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_FAIL(drop_schemas_())) {
  } else if (OB_FAIL(create_schemas_())) {
  }
  return ret;
}
int ObCreateViewHelper::clean_on_fail_commit_()
{
  // do nothing
  return OB_SUCCESS;
}
int ObCreateViewHelper::operation_before_commit_()
{
  // do nothing
  return OB_SUCCESS;
}

int ObCreateViewHelper::construct_and_adjust_result_(int &return_ret)
{
  int ret = return_ret;
  if (FAILEDx(check_inner_stat_())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    ObSchemaVersionGenerator *tsi_generator = GET_TSI(TSISchemaVersionGenerator);
    if (OB_ISNULL(tsi_generator)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tsi schema version generator is null", KR(ret));
    } else if (OB_UNLIKELY(OB_ISNULL(new_view_schema_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new view schema is null", KR(ret));
    } else if (!new_view_schema_->is_valid()) {
      ret = OB_NOT_INIT;
      LOG_WARN("new view schema not ready", KR(ret));
    } else {
      tsi_generator->get_current_version(res_.schema_version_);
      res_.table_id_ = new_view_schema_->get_table_id();
    }
  }
  return ret;
}

//TODO:(yanmu.ztl) to implement
