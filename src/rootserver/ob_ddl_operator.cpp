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

#include "ob_ddl_operator.h"
#include "share/ob_autoincrement_service.h"
#include "rootserver/ob_dependency_ddl_helper.h"
#include "lib/encrypt/ob_encrypted_helper.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "sql/resolver/ddl/ob_vec_index_builder_util.h"
#include "rootserver/ob_ddl_sql_generator.h"
#include "rootserver/ob_local_management_service.h"
#include "share/ob_sql_client_decorator.h"
#include "share/schema/ob_database_sql_service.h"
#include "rootserver/ob_tablet_drop.h"
#include "share/schema/ob_outline_sql_service.h"
#include "share/schema/ob_priv_sql_service.h"
#include "share/schema/ob_routine_sql_service.h"
#include "share/schema/ob_sys_variable_sql_service.h"
#include "share/schema/ob_table_sql_service.h"
#include "share/schema/ob_user_sql_service.h"
#include "sql/optimizer/stat/ob_dbms_stats_maintenance_window.h"
#include "pl/pl_cache/ob_pl_cache_mgr.h"
#include "share/schema/ob_dependency_info.h"  // relocated-definition owner
#include "share/schema/ob_multi_version_schema_service.h"  // relocated-definition owner

namespace oceanbase
{

using namespace common;
using namespace share;
using namespace share::schema;
using namespace obcall;
using namespace sql;
using namespace storage;

namespace rootserver
{


ObSysStat::Item::Item(ObSysStat::ItemList &list, const char *name, const char *info)
  : name_(name), info_(info)
{
  value_.set_int(0);
  const bool add_success = list.add_last(this);
  if (!add_success) {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "add last failed");
  }
}

#define MAX_ID_NAME_INFO(id) ObMaxIdFetcher::get_max_id_name(id), ObMaxIdFetcher::get_max_id_info(id)
ObSysStat::ObSysStat()
  : ob_max_used_server_id_(item_list_, MAX_ID_NAME_INFO(OB_MAX_USED_SERVER_ID_TYPE)),
    ob_max_used_ddl_task_id_(item_list_, MAX_ID_NAME_INFO(OB_MAX_USED_DDL_TASK_ID_TYPE)),
    ob_max_used_normal_rowid_table_tablet_id_(item_list_, MAX_ID_NAME_INFO(OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE)),
    ob_max_used_sys_pl_object_id_(item_list_, MAX_ID_NAME_INFO(OB_MAX_USED_SYS_PL_OBJECT_ID_TYPE)),
    ob_max_used_object_id_(item_list_, MAX_ID_NAME_INFO(OB_MAX_USED_OBJECT_ID_TYPE))
{
}

// set values after bootstrap
int ObSysStat::set_initial_values()
{
  int ret = OB_SUCCESS;
  {
    ob_max_used_server_id_.value_.set_int(OB_INIT_SERVER_ID - 1);
    ob_max_used_ddl_task_id_.value_.set_int(OB_INIT_DDL_TASK_ID);
  }
  if (OB_SUCC(ret)) {
    ob_max_used_normal_rowid_table_tablet_id_.value_.set_int(ObTabletID::MIN_USER_NORMAL_ROWID_TABLE_TABLET_ID);
    ob_max_used_sys_pl_object_id_.value_.set_int(OB_MIN_SYS_PL_OBJECT_ID);
    // Reserve identifiers used by the bootstrap database objects.
    ob_max_used_object_id_.value_.set_int(OB_INITIAL_TEST_DATABASE_ID);
  }
  return ret;
}

ObDDLOperator::ObDDLOperator(
    ObMultiVersionSchemaService &schema_service,
    common::ObMySQLProxy &sql_proxy)
    : schema_service_(schema_service),
      sql_proxy_(sql_proxy)
{
}

ObDDLOperator::~ObDDLOperator()
{
}

int ObDDLOperator::initialize_runtime_schema(ObServerRuntimeSchema &runtime_schema)
{
  int ret = OB_SUCCESS;
  int64_t new_schema_version = OB_INVALID_VERSION;
  if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    runtime_schema.set_schema_version(new_schema_version);
    runtime_schema.set_status(SERVER_RUNTIME_STATUS_NORMAL);
  }
  LOG_INFO("initialize runtime schema", K(ret), K(runtime_schema));
  return ret;
}

int ObDDLOperator::replace_sys_variable(ObSysVariableSchema &sys_variable_schema,
                                        const int64_t schema_version,
                                        ObMySQLTransaction &trans,
                                        const ObSchemaOperationType &operation_type,
                                        const common::ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  int64_t start = ObTimeUtility::current_time();
  sys_variable_schema.set_schema_version(schema_version);
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();

  if (schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid schema_version", K(ret), K(schema_version));
  } else if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service_impl must not null");
  } else if (OB_FAIL(schema_service_impl->get_sys_variable_sql_service()
                     .replace_sys_variable(sys_variable_schema, trans, operation_type, ddl_stmt_str))) {
  }
  LOG_INFO("replace sys variable", K(ret),
           "cost", ObTimeUtility::current_time() - start);
  return ret;
}

int ObDDLOperator::create_database(ObDatabaseSchema &database_schema,
                                   ObMySQLTransaction &trans,
                                   const ObString *ddl_stmt_str/*=NULL*/)
{
  int ret = OB_SUCCESS;
  //set the old database id

  int64_t new_schema_version = OB_INVALID_VERSION;
  uint64_t new_database_id = database_schema.get_database_id();
  ObSchemaService *schema_service = schema_service_.get_schema_service();

  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service must not null");
  } else if (OB_FAIL(schema_service->fetch_new_database_id(new_database_id))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    database_schema.set_database_id(new_database_id);
    database_schema.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_database_sql_service().insert_database(
        database_schema, trans, ddl_stmt_str))) {
    }
  }
  return ret;
}

int ObDDLOperator::alter_database(ObDatabaseSchema &new_database_schema,
                                  ObMySQLTransaction &trans,
                                  const ObSchemaOperationType op_type,
                                  const ObString *ddl_stmt_str/*=NULL*/,
                                  const bool need_update_schema_version/*=true*/)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    RS_LOG(ERROR, "schema_service must not null");
  } else {
    if (need_update_schema_version) {

      int64_t new_schema_version = OB_INVALID_VERSION;
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else {
        new_database_schema.set_schema_version(new_schema_version);
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(schema_service->get_database_sql_service()
         .update_database(new_database_schema,
                          trans,
                          op_type,
                          ddl_stmt_str))) {
    }
  }
  return ret;
}

int ObDDLOperator::drop_database(const ObDatabaseSchema &db_schema,
                                 ObMySQLTransaction &trans,
                                 const ObString *ddl_stmt_str/*=NULL*/)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();

  const uint64_t database_id = db_schema.get_database_id();
  int64_t new_schema_version = OB_INVALID_VERSION;
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", OB_P(schema_service_impl), K(ret));
  }
  //drop tables in recyclebin
  if (OB_SUCC(ret)) {
    if (OB_FAIL(purge_table_of_database(db_schema, trans))) {
    }
  }
  //delete triggers in database, only delete trigger_database != base_table_database triggers
  // trigger_database == base_table_database's trigger will be deleted when the table is deleted below
  OZ (ObPLDDLOperator::drop_trigger_in_drop_database(db_schema, *this, trans));

  // delete tables in database
  if (OB_SUCC(ret)) {
    ObArray<uint64_t> table_ids;
    ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_table_ids_in_database(database_id, table_ids))) {
    } else {
      // drop index tables first
      for (int64_t cycle = 0; OB_SUCC(ret) && cycle < 2; ++cycle) {
        for (int64_t i = 0; OB_SUCC(ret) && i < table_ids.count(); ++i) {
          const ObTableSchema *table = NULL;
          const uint64_t table_id = table_ids.at(i);
          if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
          } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table))) {
          } else if (OB_ISNULL(table)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("table is NULL", K(ret));
          } else if (table->is_in_recyclebin()) {
            // already been dropped before
          } else {
            bool is_delete_first = table->is_aux_table();
            if ((0 == cycle ? is_delete_first : !is_delete_first)) {
              // drop triggers before drop table
              if (OB_FAIL(ObPLDDLOperator::drop_trigger_cascade(*table, trans, *this))) {
              } else if (OB_FAIL(drop_table(*table, trans, NULL, false, NULL, true))) {
              }
            }
          }
        }
      }
    }
  }

  // delete outlines in database
  if (OB_SUCC(ret)) {
    ObArray<const ObSimpleOutlineSchema *> outline_schemas;
    ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_simple_outline_schemas_in_database(database_id, outline_schemas))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < outline_schemas.count(); ++i) {
        const ObSimpleOutlineSchema *outline_schema = outline_schemas.at(i);
        if (OB_ISNULL(outline_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("outline info is NULL", K(ret));
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_service_impl->get_outline_sql_service().delete_outline(database_id,
                                                                                         outline_schema->get_outline_id(),
                                                                                         new_schema_version, trans))) {
        }
      }
    }
  }

  // delete packages in database
  if (OB_SUCC(ret)) {
    ObSchemaGetterGuard schema_guard;
    ObArray<const ObSimplePackageSchema*> package_schemas;
    if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_simple_package_schemas_in_database(database_id, package_schemas))) {
    } else {
       common::ObSqlString public_sql_string;
       for (int64_t i = 0; OB_SUCC(ret) && i < package_schemas.count(); ++i) {
         const ObSimplePackageSchema *package_schema = package_schemas.at(i);
         if (OB_ISNULL(package_schema)) {
           ret = OB_ERR_UNEXPECTED;
           LOG_WARN("package info is NULL", K(ret));
         } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
         } else if (OB_FAIL(schema_service_impl->get_routine_sql_service().drop_package(
                                                                           package_schema->get_database_id(),
                                                                           package_schema->get_package_id(),
                                                                           new_schema_version, trans))) {
         }
       }
     }
   }

  // delete routines in database
  if (OB_SUCC(ret)) {
    ObArray<uint64_t> routine_ids;
    ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_routine_ids_in_database( database_id, routine_ids))) {
    } else {
      common::ObSqlString public_sql_string;
      for (int64_t i = 0; OB_SUCC(ret) && i < routine_ids.count(); ++i) {
        const ObRoutineInfo *routine_info = NULL;
        const uint64_t routine_id = routine_ids.at(i);
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
        } else if (OB_FAIL(schema_guard.get_routine_info( routine_id, routine_info))) {
        } else if (OB_ISNULL(routine_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("routine info is NULL", K(ret));
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_service_impl->get_routine_sql_service().drop_routine(
                           *routine_info, new_schema_version, trans))) {
        }
      }
    }
  }

  // flush pl cache
  OZ (pl::ObPLCacheMgr::flush_pl_cache_by_sql(OB_INVALID_ID, database_id, schema_service_));

  // delete mock_fk_parent_tables in database
  if (OB_SUCC(ret)) {
    ObSchemaGetterGuard schema_guard;
    ObArray<uint64_t> mock_fk_parent_table_ids;
    if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_mock_fk_parent_table_ids_in_database(database_id, mock_fk_parent_table_ids))) {
    } else {
      ObArray<ObMockFKParentTableSchema> mock_fk_parent_table_schema_array;
      int64_t new_schema_version = OB_INVALID_VERSION;
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < mock_fk_parent_table_ids.count(); ++i) {
        ObMockFKParentTableSchema tmp_mock_fk_parent_table_schema;
        const uint64_t mock_fk_parent_table_id = mock_fk_parent_table_ids.at(i);
        const ObMockFKParentTableSchema *mock_fk_parent_table_schema = NULL;
        if (OB_FAIL(schema_guard.get_mock_fk_parent_table_schema_with_id(mock_fk_parent_table_id,
                                                                         mock_fk_parent_table_schema))) {
        } else if (OB_ISNULL(mock_fk_parent_table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("mock fk parent table schema is NULL", KR(ret), K(mock_fk_parent_table_id));
        } else if (OB_FAIL(tmp_mock_fk_parent_table_schema.assign(*mock_fk_parent_table_schema))) {
        } else if (FALSE_IT(tmp_mock_fk_parent_table_schema.set_schema_version(new_schema_version))) {
        } else if (FALSE_IT(tmp_mock_fk_parent_table_schema.set_operation_type(ObMockFKParentTableOperationType::MOCK_FK_PARENT_TABLE_OP_DROP_TABLE))) {
        } else if (OB_FAIL(mock_fk_parent_table_schema_array.push_back(tmp_mock_fk_parent_table_schema))) {
        }
      }
      if (FAILEDx(deal_with_mock_fk_parent_tables(trans, schema_guard, mock_fk_parent_table_schema_array))) {
        LOG_WARN("drop mock_fk_parent_table failed", K(ret), K(mock_fk_parent_table_schema_array));
      }
    }
  }

  if (OB_SUCC(ret)) {
    int64_t new_schema_version = OB_INVALID_VERSION;
    if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else if (OB_FAIL(schema_service_impl->get_database_sql_service().delete_database(
        db_schema,
        new_schema_version,
        trans,
        ddl_stmt_str))) {
    }
  }
  return ret;
}

// When delete database to recyclebin, it is necessary to update schema version
// of each table, in case of hitting plan cache.
// The key of plan cache is current database name, table_id and schema version.
int ObDDLOperator::update_table_version_of_db(const ObDatabaseSchema &database_schema,
                                              ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  const uint64_t database_id = database_schema.get_database_id();
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObArray<uint64_t> table_ids;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service should not be null", K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_ids_in_database(database_id,
                                                            table_ids))) {
  }
  const int64_t table_count = table_ids.count();
  for (int64_t idx = 0; OB_SUCC(ret) && idx < table_count; ++idx) {
    const ObTableSchema *table = NULL;
    if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_table_schema( table_ids.at(idx), table))) {
    } else if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table schema should not be null", K(ret));
    } else if (table->is_index_table()) {
      continue;
    } else {
      ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
      if (OB_FAIL(table->get_simple_index_infos(simple_index_infos))) {
      }
      ObSchemaGetterGuard tmp_schema_guard;
      for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
        const ObTableSchema *index_table_schema = NULL;
        const uint64_t table_id = simple_index_infos.at(i).table_id_;
        if (OB_FAIL(schema_service_.get_runtime_schema_guard(tmp_schema_guard))) {
        } else if (OB_FAIL(tmp_schema_guard.get_table_schema(
                                                             table_id,
                                                             index_table_schema))) {
        } else if (OB_ISNULL(index_table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table schema should not be null", K(ret));
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else {
          HEAP_VAR(ObTableSchema, new_index_schema) {
            if (OB_FAIL(new_index_schema.assign(*index_table_schema))) {
            } else {
              new_index_schema.set_schema_version(new_schema_version);
            }
            if (FAILEDx(schema_service->get_table_sql_service().update_table_options(
                trans,
                *index_table_schema,
                new_index_schema,
                OB_DDL_DROP_TABLE_TO_RECYCLEBIN,
                NULL))) {
              LOG_WARN("update_table_option failed", KR(ret), K(table_id));
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        HEAP_VAR(ObTableSchema, new_ts) {
          ObSchemaOperationType op_type;
          if (OB_FAIL(new_ts.assign(*table))) {
          } else {
            op_type = new_ts.is_view_table() ? OB_DDL_DROP_VIEW_TO_RECYCLEBIN : OB_DDL_DROP_TABLE_TO_RECYCLEBIN;
          }
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
          } else {
            new_ts.set_schema_version(new_schema_version);
            if (OB_FAIL(schema_service->get_table_sql_service().update_table_options(
                trans, *table, new_ts, op_type, NULL))) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::drop_database_to_recyclebin(const ObDatabaseSchema &database_schema,
                                               ObMySQLTransaction &trans,
                                               const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;

  const uint64_t database_id = database_schema.get_database_id();
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("schema_service_impl must not null", K(ret));
  } else {
    ObSqlString new_db_name;
    ObRecycleObject recycle_object;
    ObDatabaseSchema new_database_schema;
    ObSchemaService *schema_service = schema_service_.get_schema_service();
    recycle_object.set_type(ObRecycleObject::DATABASE);
    recycle_object.set_database_id(database_schema.get_database_id());
    recycle_object.set_table_id(OB_INVALID_ID);
    if (OB_FAIL(recycle_object.set_original_name(database_schema.get_database_name_str()))) {
    } else if (OB_FAIL(new_database_schema.assign(database_schema))) {
    } else if (FALSE_IT(new_database_schema.set_in_recyclebin(true))) {
     // It ensure that db schema version of insert recyclebin and alter database
     // is equal that updating table version and inserting recyclebin.
    } else if (OB_FAIL(update_table_version_of_db(database_schema, trans))) {
    } else if (OB_ISNULL(schema_service)) {
      ret = OB_ERR_SYS;
      LOG_WARN("schema service should not be NULL");
    } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else if (FALSE_IT(new_database_schema.set_schema_version(new_schema_version))) {
    } else if (OB_FAIL(construct_new_name_for_recyclebin(new_database_schema, new_db_name))) {
    } else if (OB_FAIL(new_database_schema.set_database_name(new_db_name.string()))) {
    } else if (FALSE_IT(recycle_object.set_object_name(new_db_name.string()))) {
    } else if (OB_FAIL(schema_service_impl->insert_recyclebin_object(recycle_object,
                                                              trans))) {
    } else if (OB_FAIL(alter_database(new_database_schema, trans,
                                      OB_DDL_DROP_DATABASE_TO_RECYCLEBIN,
                                      ddl_stmt_str,
                                      false /*no need_new_schema_version*/))) {
    } else {
      ObSchemaGetterGuard schema_guard;
      ObArray<const ObSimpleTableSchemaV2 *> tables;
      if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_table_schemas_in_database(database_id,
                                                                    tables))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
        const ObSimpleTableSchemaV2 *table_schema = tables.at(i);
        if (OB_ISNULL(table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table is NULL", K(ret));
        } else if (table_schema->is_view_table()
                  && OB_FAIL(ObDependencyInfo::delete_schema_object_dependency(
                            trans,
                            table_schema->get_table_id(),
                            table_schema->get_schema_version(),
                            ObObjectType::VIEW))) {
          LOG_WARN("failed to delete_schema_object_dependency", K(ret), K(1UL),
          K(table_schema->get_table_id()));
        }
      }
    }
  }
  return ret;
}

// todo
int ObDDLOperator::get_user_id_for_inner_ur(ObUserInfo &user,
                                            bool &is_inner_ur,
                                            uint64_t &new_user_id)
{
  int ret = OB_SUCCESS;
  is_inner_ur = false;
  return ret;
}

int ObDDLOperator::create_user(ObUserInfo &user,
                               const ObString *ddl_stmt_str,
                               ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  uint64_t new_user_id = user.get_user_id();
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  bool is_inner_ur;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service must not null");
  } else if (OB_FAIL(get_user_id_for_inner_ur(user, is_inner_ur, new_user_id))) {
  } else if (!is_inner_ur &&
             OB_FAIL(schema_service->fetch_new_user_id(new_user_id))) {
    LOG_WARN("failed to fetch_new_user_id",  K(ret));
  } else {
    user.set_user_id(new_user_id);
  }
  if (OB_SUCC(ret)) {

    int64_t new_schema_version = OB_INVALID_VERSION;
    if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else if (OB_FAIL(schema_service->get_user_sql_service().create_user(
               user, new_schema_version, ddl_stmt_str, trans))) {
    }
  }
  return ret;
}

int ObDDLOperator::create_table(ObTableSchema &table_schema,
                                ObMySQLTransaction &trans,
                                const ObString *ddl_stmt_str/*=NULL*/,
                                const bool need_sync_schema_version,
                                const bool is_truncate_table /*false*/)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    RS_LOG(ERROR, "schema_service must not null");
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    table_schema.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_table_sql_service().create_table(
        table_schema,
        trans,
        ddl_stmt_str,
        need_sync_schema_version,
        is_truncate_table))) {
    } else if (OB_FAIL(sync_version_for_cascade_table(table_schema.get_depend_table_ids(), trans))) {
    } else if (OB_FAIL(sync_version_for_cascade_mock_fk_parent_table(table_schema.get_depend_mock_fk_parent_table_ids(), trans))) {
    }
  }

  if (OB_SUCC(ret) && (table_schema.is_vec_delta_buffer_type() ||
      table_schema.is_hybrid_vec_index_log_type()) &&
      OB_FAIL(ObVectorIndexUtil::add_dbms_vector_jobs(trans,
                                                      table_schema.get_table_id(),
                                                      table_schema.get_exec_env()))) {
    LOG_WARN("failed to add dbms_vector jobs", K(ret), K(table_schema));
  }
  return ret;
}

int ObDDLOperator::sync_version_for_cascade_table(const ObIArray<uint64_t> &table_ids,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  uint64_t id = OB_INVALID_ID;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    RS_LOG(ERROR, "schema_service must not null");
  } else {
    for (int64_t i = 0; i < table_ids.count() && OB_SUCC(ret); i++) {
      id = table_ids.at(i);
      int64_t new_schema_version = OB_INVALID_VERSION;
      int64_t old_schema_version = OB_INVALID_VERSION;
      HEAP_VAR(ObTableSchema, table_schema) {
        ObRefreshSchemaStatus schema_status;

        if (OB_FAIL(schema_service->get_table_schema_from_inner_table(
                      schema_status, id, trans, table_schema))) {
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else {
          old_schema_version = table_schema.get_schema_version();
          if (OB_FAIL(schema_service->get_table_sql_service().sync_schema_version_for_history(
                      trans,
                      table_schema,
                      new_schema_version))) {
          } else {
            LOG_INFO("synced schema version for depend table", K(id), "from", old_schema_version, "to", new_schema_version);
          }
        }
      }
    }
  }

  return ret;
}


int ObDDLOperator::reinit_autoinc_row(const ObTableSchema &table_schema,
                                      common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  uint64_t table_id = table_schema.get_table_id();
  ObString table_name = table_schema.get_table_name();
  int64_t truncate_version = table_schema.get_truncate_version();
  uint64_t column_id = table_schema.get_autoinc_column_id();
  ObAutoincrementService &autoinc_service = share::ObAutoincrementService::get_instance();

  if (0 != column_id) {
    // reinit auto_increment value
    if (OB_FAIL(autoinc_service.reinit_autoinc_row(table_id,
                                                   column_id, truncate_version, trans))) {
    }
  }
  int64_t finish_time = ObTimeUtility::current_time();
  LOG_INFO("finish reinit_auto_row", KR(ret), "cost_ts", finish_time - start_time);
  return ret;
}

int ObDDLOperator::try_reinit_autoinc_row(const ObTableSchema &table_schema,
                                          common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  bool need_reinit_inner_table = false;
  const uint64_t table_id = table_schema.get_table_id();

  const int64_t truncate_version = table_schema.get_truncate_version();
  const uint64_t column_id = table_schema.get_autoinc_column_id();
  ObAutoincrementService &autoinc_service = share::ObAutoincrementService::get_instance();
  if (OB_FAIL(autoinc_service.try_lock_autoinc_row(table_id, column_id, truncate_version,
                                                    need_reinit_inner_table, trans))) {
  } else if (need_reinit_inner_table) {
    if (OB_FAIL(autoinc_service.reset_autoinc_row(table_id, column_id,
                                                  truncate_version, trans))) {
    }
  }
  return ret;
}

int ObDDLOperator::update_prev_id_for_delete_column(const ObTableSchema &origin_table_schema,
    ObTableSchema &new_table_schema,
    const ObColumnSchemaV2 &ori_column_schema,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  const bool need_del_stats = false;
  // When a transaction currently add/drop column: origin_table_schema don't update prev&next column ID, so it need fetch from new table.
  ObColumnSchemaV2 *new_origin_col = new_table_schema.get_column_schema(ori_column_schema.get_column_name());
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    RS_LOG(ERROR, "schema_service must not null");
  } else if (OB_ISNULL(new_origin_col)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Failed to get column from new table schema", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(new_table_schema.delete_column_update_prev_id(new_origin_col))) {
  } else {
    ObColumnSchemaV2 *next_col = new_table_schema.get_column_schema_by_prev_next_id(new_origin_col->get_next_column_id());
    if (OB_ISNULL(next_col)) {
      // do nothing since local_column is tail column
    } else {
      next_col->set_schema_version(new_schema_version);
      if (OB_FAIL(schema_service->get_table_sql_service().update_single_column(
          trans,
          origin_table_schema,
          new_table_schema,
          *next_col,
          true /* record_ddl_operation */,
          need_del_stats))) {
      }
    }
  }
  return ret;
}

int ObDDLOperator::update_table_foreign_keys(share::schema::ObTableSchema &new_table_schema,
                                             common::ObMySQLTransaction &trans,
                                             bool in_offline_ddl_white_list)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  int64_t new_schema_version = OB_INVALID_VERSION;


  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (FALSE_IT(new_table_schema.set_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().update_foreign_key_state(
             trans, new_table_schema))) {
  } else {
    uint64_t id = OB_INVALID_ID;
    const ObTableSchema *schema = NULL;
    const ObIArray<uint64_t> &table_ids = new_table_schema.get_depend_table_ids();
    for (int64_t i = 0; i < table_ids.count() && OB_SUCC(ret); i++) {
      ObSchemaGetterGuard schema_guard;
      id = table_ids.at(i);
      ObTableSchema tmp_schema;
      if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_table_schema( id, schema))) {
      } else if (!schema) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema is NULL", K(ret));
      } else if (OB_FAIL(tmp_schema.assign(*schema))) {
      } else if (FALSE_IT(tmp_schema.set_in_offline_ddl_white_list(in_offline_ddl_white_list))) {
      } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_service->get_table_sql_service().sync_schema_version_for_history(
                trans,
                tmp_schema,
                new_schema_version))) {
      } else {
        ObSchemaOperationType operation_type = OB_DDL_ALTER_TABLE;
        if (OB_FAIL(update_table_attribute(new_table_schema,
                                          trans,
                                          operation_type))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::add_table_foreign_keys(const share::schema::ObTableSchema &orig_table_schema,
                                          share::schema::ObTableSchema &inc_table_schema,
                                          common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  int64_t new_schema_version = OB_INVALID_VERSION;


  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    inc_table_schema.set_schema_version(new_schema_version);
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(schema_service->get_table_sql_service().add_foreign_key(trans, inc_table_schema, false))) {
    } else if (OB_FAIL(schema_service->get_table_sql_service().update_foreign_key_state(trans, inc_table_schema))) {
    } else if (OB_FAIL(sync_version_for_cascade_table(inc_table_schema.get_depend_table_ids(), trans))) {
    } else if (OB_FAIL(sync_version_for_cascade_mock_fk_parent_table(inc_table_schema.get_depend_mock_fk_parent_table_ids(), trans))) {
    }
  }

  return ret;
}

int ObDDLOperator::modify_check_constraints_state(
    const ObTableSchema &orig_table_schema,
    const ObTableSchema &inc_table_schema,
    ObTableSchema &new_table_schema,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  UNUSED(orig_table_schema);

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObTableSchema::const_constraint_iterator iter = inc_table_schema.constraint_begin();

  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (inc_table_schema.constraint_end() == iter) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table doesn't have a check constraint", K(ret), K(inc_table_schema));
  } else if (inc_table_schema.constraint_end() != iter + 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("update check constraint state couldn't be executed with other DDLs", K(ret), K(inc_table_schema));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    (*iter)->set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_table_sql_service().update_check_constraint_state(trans, new_table_schema, **iter))) {
    }
  }

  return ret;
}

int ObDDLOperator::add_table_constraints(const ObTableSchema &inc_table_schema,
                                         ObTableSchema &new_table_schema,
                                         ObMySQLTransaction &trans,
                                         ObSArray<uint64_t> *cst_ids/*NULL*/)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  }
  for (ObTableSchema::const_constraint_iterator iter = inc_table_schema.constraint_begin(); OB_SUCC(ret) &&
    iter != inc_table_schema.constraint_end(); iter ++) {
    uint64_t new_cst_id = OB_INVALID_ID;
    if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else if (OB_FAIL(schema_service->fetch_new_constraint_id(new_cst_id))) {
    } else {
      (*iter)->set_schema_version(new_schema_version);
      (*iter)->set_table_id(new_table_schema.get_table_id());
      (*iter)->set_constraint_id(new_cst_id);
      (*iter)->set_constraint_type((*iter)->get_constraint_type());
      if (OB_FAIL(schema_service->get_table_sql_service().insert_single_constraint(trans, new_table_schema, **iter))) {
      } else {
        if (OB_NOT_NULL(cst_ids)) {
          OZ(cst_ids->push_back(new_cst_id));
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::add_table_partitions(const ObTableSchema &orig_table_schema,
                                        ObTableSchema &inc_table_schema,
                                        ObTableSchema &new_table_schema,
                                        ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().add_inc_partition_info(trans,
                                                                                    orig_table_schema,
                                                                                    inc_table_schema,
                                                                                    new_schema_version,
                                                                                    false,
                                                                                    false))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    const int64_t part_num = orig_table_schema.get_part_option().get_part_num();
    const int64_t inc_part_num = inc_table_schema.get_partition_num();
    const int64_t all_part_num = part_num + inc_part_num;
    new_table_schema.get_part_option().set_part_num(all_part_num);
    new_table_schema.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_table_sql_service()
                           .update_partition_option(trans, new_table_schema))) {
    }
  }
  return ret;
}

int ObDDLOperator::add_table_subpartitions(const ObTableSchema &orig_table_schema,
                                           ObTableSchema &inc_table_schema,
                                           ObTableSchema &new_table_schema,
                                           ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObArray<ObPartition*> update_part_array;
  //FIXME:should move the related logic to ObDDLService
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(get_part_array_from_table(new_table_schema, inc_table_schema, update_part_array))) {
  } else if (update_part_array.count() <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("update part array count is not more than 0", KR(ret), K(update_part_array.count()));
  } else if (update_part_array.count() != inc_table_schema.get_partition_num()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("update_part_array count not equal inc_table part count",
              KR(ret), K(update_part_array.count()), K(inc_table_schema.get_partition_num()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < update_part_array.count(); i++) {
      ObPartition *part = update_part_array.at(i);
      if (OB_ISNULL(part)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("update_part_array[i]", KR(ret), K(i));
      } else if (i >= inc_table_schema.get_partition_num()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("update_part_array[i] out of inc_part_array", KR(ret), K(i), K(inc_table_schema.get_partition_num()));
      } else if (OB_ISNULL(inc_table_schema.get_part_array()[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inc_table_part_array[i]", KR(ret), K(i));
      } else {
        const int64_t subpart_num = part->get_subpartition_num();
        const int64_t inc_subpart_num = inc_table_schema.get_part_array()[i]->get_subpartition_num();
        part->set_sub_part_num(subpart_num + inc_subpart_num);
        part->set_schema_version(new_schema_version);
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_service->get_table_sql_service().add_inc_partition_info(trans,
                                                          orig_table_schema,
                                                          inc_table_schema,
                                                          new_schema_version,
                                                          false,
                                                          true))) {
      }
      new_table_schema.set_schema_version(new_schema_version);
      if (FAILEDx(schema_service->get_table_sql_service().update_subpartition_option(trans,
          new_table_schema, update_part_array))) {
        LOG_WARN("update sub partition option failed");
      }
    }
  }
  return ret;
}

int ObDDLOperator::truncate_table(const ObString *ddl_stmt_str,
                                  const share::schema::ObTableSchema &orig_table_schema,
                                  const share::schema::ObTableSchema &new_table_schema,
                                  common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  bool is_truncate_table = true;
  bool is_truncate_partition = false;
  uint64_t table_id = new_table_schema.get_table_id();
  uint64_t schema_version = new_table_schema.get_schema_version();
  ObSchemaOperationType operation_type = OB_DDL_TRUNCATE_TABLE;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", KR(ret));
  } else if (new_table_schema.is_partitioned_table()) {
    if (OB_INVALID_VERSION == schema_version) {
      ret  = OB_ERR_UNEXPECTED;
      LOG_WARN("schema version is not legal", KR(ret), K(table_id), K(schema_version));
    } else if (OB_FAIL(schema_service->get_table_sql_service()
                                      .drop_inc_part_info(trans,
                                                          orig_table_schema,
                                                          orig_table_schema,
                                                          schema_version,
                                                          is_truncate_partition,
                                                          is_truncate_table))) {
    } else if (OB_FAIL(schema_service->get_table_sql_service()
                                      .add_inc_part_info(trans,
                                                        orig_table_schema,
                                                        new_table_schema,
                                                        schema_version,
                                                        is_truncate_table))) {
    }
  }
  if (FAILEDx(schema_service->get_table_sql_service()
                            .update_table_attribute(trans,
                                                    new_table_schema,
                                                    operation_type,
                                                    false,
                                                    ddl_stmt_str))) {
    LOG_WARN("failed to update table schema attribute", KR(ret), K(table_id), K(schema_version));
  }
  return ret;
}

int ObDDLOperator::update_boundary_schema_version(const uint64_t &boundary_schema_version,
                                                  common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();

  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", KR(ret));
  } else {
    ObSchemaOperation schema_operation;

    schema_operation.op_type_ = OB_DDL_END_SIGN;
    share::schema::ObDDLSqlService ddl_sql_service(*schema_service);

    if (OB_FAIL(ddl_sql_service.log_nop_operation(schema_operation,
                                                  boundary_schema_version,
                                                  NULL,
                                                  trans))) {
    }
  }
  return ret;
}

int ObDDLOperator::inc_table_schema_version(ObMySQLTransaction &trans,
                                            const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();

  if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id));
  } else if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("schema service should not be NULL", K(ret));
  } else if (OB_FAIL(schema_service->get_table_sql_service().
                                        update_data_table_schema_version(trans,
                                                                         table_id,
                                                                         false))) {
  }
  return ret;
}

// Truncating partitions updates __all_part and __all_table.
int ObDDLOperator::truncate_table_partitions(const share::schema::ObTableSchema &orig_table_schema,
                                             share::schema::ObTableSchema &inc_table_schema,
                                             share::schema::ObTableSchema &del_table_schema,
                                             common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", KR(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().truncate_part_info(
                     trans,
                     orig_table_schema,
                     inc_table_schema,
                     del_table_schema,
                     new_schema_version))) {
  }

  return ret;
}

int ObDDLOperator::truncate_table_subpartitions(const share::schema::ObTableSchema &orig_table_schema,
                                                share::schema::ObTableSchema &inc_table_schema,
                                                share::schema::ObTableSchema &del_table_schema,
                                                common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", KR(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().truncate_subpart_info(
                     trans,
                     orig_table_schema,
                     inc_table_schema,
                     del_table_schema,
                     new_schema_version))) {
  }

  return ret;
}


int ObDDLOperator::get_part_array_from_table(const ObTableSchema &new_table_schema,
                                             const ObTableSchema &inc_table_schema,
                                             ObIArray<ObPartition*> &out_part_array)
{
  int ret = OB_SUCCESS;

  ObPartition **inc_part_array = inc_table_schema.get_part_array();
  const int64_t inc_part_sum = inc_table_schema.get_partition_num();
  for (int64_t i = 0; OB_SUCC(ret) && i < inc_part_sum; i++) {
    ObPartition *inc_part = inc_part_array[i];
    if (OB_ISNULL(inc_part)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inc_part_array[i] is null", KR(ret), K(i));
    } else {
      ObPartition **part_array = new_table_schema.get_part_array();
      const int64_t part_sum = new_table_schema.get_partition_num();
      int64_t j = 0;
      for (j = 0; OB_SUCC(ret) && j < part_sum; j++) {
        ObPartition *part = part_array[j];
        if (OB_ISNULL(part)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("part_array[j] is NULL", K(ret), K(j));
        } else if (part->get_part_id() == inc_part->get_part_id()) {
          if (OB_FAIL(out_part_array.push_back(part))) {
          }
          break;
        }
      }
      if (OB_SUCC(ret) && j >= part_sum) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inc_part_array[i] is not in part_array",
                 KR(ret), K(i), KPC(inc_part), K(new_table_schema));
      }
    }
  }
  return ret;
}

bool ObDDLOperator::is_list_values_equal(const common::ObRowkey &fir_values,
                                         const common::ObIArray<common::ObNewRow> &sed_values)
{
  bool equal = false;
  int64_t s_count = sed_values.count();
  common::ObRowkey rowkey;
  for (int64_t j = 0; j < s_count; ++j) {
    rowkey.reset();
    rowkey.assign(sed_values.at(j).cells_, sed_values.at(j).count_);
    if (fir_values == rowkey) {
      equal = true;
      break;
    }
  }
  return equal;
}


int ObDDLOperator::rename_table_partitions(const ObTableSchema &orig_table_schema,
                                         ObTableSchema &inc_table_schema,
                                         ObTableSchema &new_table_schema,
                                         ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", KR(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().rename_inc_part_info(trans,
                                                                          orig_table_schema,
                                                                          inc_table_schema,
                                                                          new_schema_version,
                                                                          false))) {
  }
  return ret;
}

int ObDDLOperator::rename_table_subpartitions(const ObTableSchema &orig_table_schema,
                                         ObTableSchema &inc_table_schema,
                                         ObTableSchema &new_table_schema,
                                         ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", KR(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().rename_inc_subpart_info(trans,
                                                                          orig_table_schema,
                                                                          inc_table_schema,
                                                                          new_schema_version))) {
  }
  return ret;
}

int ObDDLOperator::drop_table_partitions(const ObTableSchema &orig_table_schema,
                                         ObTableSchema &inc_table_schema,
                                         ObTableSchema &new_table_schema,
                                         ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  bool is_truncate_table = false;
  bool is_truncate_partition = false;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().drop_inc_part_info(trans,
                                                                         orig_table_schema,
                                                                         inc_table_schema,
                                                                         new_schema_version,
                                                                         is_truncate_partition,
                                                                         is_truncate_table))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    const int64_t part_num = orig_table_schema.get_part_option().get_part_num();
    const int64_t inc_part_num = inc_table_schema.get_partition_num();
    const int64_t all_part_num = part_num - inc_part_num;
    new_table_schema.get_part_option().set_part_num(all_part_num);
    new_table_schema.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_table_sql_service()
                            .update_partition_option(trans, new_table_schema))) {
    }
  }
  return ret;
}

int ObDDLOperator::drop_table_subpartitions(const ObTableSchema &orig_table_schema,
                                            ObTableSchema &inc_table_schema,
                                            ObTableSchema &new_table_schema,
                                            ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().drop_inc_subpart_info(trans,
                                                                         orig_table_schema,
                                                                         inc_table_schema,
                                                                         new_schema_version))) {
  } else {
    //FIXME:should move the related logic to ObDDLService
    ObArray<ObPartition*> update_part_array;
    if (OB_FAIL(get_part_array_from_table(new_table_schema, inc_table_schema, update_part_array))) {
    } else if (update_part_array.count() <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("update_part_array count less than 0", K(ret), K(update_part_array.count()));
    } else if (update_part_array.count() != inc_table_schema.get_partition_num()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("update_part_array count not equal inc_table part count",
                KR(ret), K(update_part_array.count()), K(inc_table_schema.get_partition_num()));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < update_part_array.count(); i++) {
        ObPartition *part = update_part_array.at(i);
        if (i >= inc_table_schema.get_partition_num()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("update_part_array[i] out of inc_part_array", KR(ret), K(i), K(inc_table_schema.get_partition_num()));
        } else if (OB_ISNULL(part)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("update_part_array[i]", KR(ret), K(i));
        } else if (OB_ISNULL(inc_table_schema.get_part_array()[i])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("inc_table_part_array[i]", KR(ret), K(i));
        } else {
          const int64_t subpart_num = part->get_sub_part_num();
          const int64_t inc_subpart_num = inc_table_schema.get_part_array()[i]->get_subpartition_num();
          part->set_sub_part_num(subpart_num - inc_subpart_num);
          part->set_schema_version(new_schema_version);
        }
      }
      if (OB_SUCC(ret)) {
        new_table_schema.set_schema_version(new_schema_version);
        if (OB_FAIL(schema_service->get_table_sql_service().update_subpartition_option(trans,
            new_table_schema, update_part_array))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::drop_table_constraints(const ObTableSchema &orig_table_schema,
                                          const ObTableSchema &inc_table_schema,
                                          ObTableSchema &new_table_schema,
                                          ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  UNUSED(orig_table_schema);

  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else {
    for (ObTableSchema::const_constraint_iterator iter = inc_table_schema.constraint_begin(); OB_SUCC(ret) &&
      iter != inc_table_schema.constraint_end(); iter ++) {
      (*iter)->set_table_id(orig_table_schema.get_table_id());
      if (nullptr == new_table_schema.get_constraint((*iter)->get_constraint_id())) {
        LOG_INFO("constraint has already been dropped", K(ret), K(**iter));
      } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_service->get_table_sql_service().delete_single_constraint(
                           new_schema_version, trans, new_table_schema, **iter))) {
      }
    }
  }
  return ret;
}


int ObDDLOperator::insert_single_column(ObMySQLTransaction &trans,
                                        const ObTableSchema &new_table_schema,
                                        ObColumnSchemaV2 &new_column)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (FALSE_IT(new_column.set_schema_version(new_schema_version))) {
    //do nothing
  } else if (OB_FAIL(schema_service->get_table_sql_service().insert_single_column(
             trans, new_table_schema, new_column, true))) {
  }
  return ret;
}

int ObDDLOperator::delete_single_column(ObMySQLTransaction &trans,
                                        const int64_t new_schema_version,
                                        ObTableSchema &new_table_schema,
                                        const ObString &column_name)
{
  int ret = OB_SUCCESS;

  ObColumnSchemaV2 *orig_column = NULL;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_VERSION == new_schema_version)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret));
  } else if (OB_ISNULL(orig_column = new_table_schema.get_column_schema(column_name))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get column schema from table failed", K(column_name));
  } else if (OB_FAIL(new_table_schema.delete_column(column_name))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().delete_single_column(
      new_schema_version, trans, new_table_schema, *orig_column, false/*need_record_ddl_operation*/))) {
  }
  return ret;
}

int ObDDLOperator::alter_table_create_index(const ObTableSchema &new_table_schema,
                                            ObIArray<ObColumnSchemaV2*> &gen_columns,
                                            ObTableSchema &index_schema,
                                            common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else {
    uint64_t index_table_id = OB_INVALID_ID;
    //index schema can't not create with specified table id
    if (OB_UNLIKELY(index_schema.get_table_id() != OB_INVALID_ID)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table_id of index should be invalid", K(ret), K(index_schema.get_table_id()));
    } else if (OB_FAIL(schema_service->fetch_new_table_id(index_table_id))) {
    } else {
      index_schema.set_table_id(index_table_id);
    }
    if (OB_SUCC(ret)) {
      if (gen_columns.empty()) {
        //create normal index table.
        if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else {
          index_schema.set_schema_version(new_schema_version);
          if (OB_FAIL(schema_service->get_table_sql_service().create_table(index_schema, trans))) {
          }
        }
      } else {
        // First increase internal generated column, and then create an index on the column
        for (int64_t i = 0; OB_SUCC(ret) && i < gen_columns.count(); ++i) {
          ObColumnSchemaV2 *new_column_schema = gen_columns.at(i);
          if (OB_ISNULL(new_column_schema)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("new column schema is null");
          } else if (OB_FAIL(insert_single_column(trans, new_table_schema, *new_column_schema))) {
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
          } else {
            index_schema.set_schema_version(new_schema_version);
            if (OB_FAIL(schema_service->get_table_sql_service().create_table(index_schema, trans))) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::alter_table_drop_index(
    const ObTableSchema *index_table_schema,
    ObTableSchema &new_data_table_schema,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  //drop inner generated index column
  ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(index_table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(index_table_schema));
  } else if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(drop_inner_generated_index_column(trans, schema_guard, *index_table_schema, new_data_table_schema))) {
  } else if (OB_FAIL(drop_table(*index_table_schema, trans))) {
  }
  if (OB_SUCC(ret)) {
    RS_LOG(INFO, "finish drop index", K(*index_table_schema), K(ret));
  }
  return ret;
}

int ObDDLOperator::alter_table_alter_index(
    const uint64_t data_table_id,
    const uint64_t database_id,
    const ObAlterIndexArg &alter_index_arg,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {

    int64_t new_schema_version = OB_INVALID_VERSION;
    RS_LOG(INFO, "start alter table alter index", K(alter_index_arg));
    const ObTableSchema *index_table_schema = NULL;
    ObString index_table_name;
    ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
    const ObString &index_name = alter_index_arg.index_name_;

    //build index name and get index schema
    if (OB_FAIL(ObTableSchema::build_index_table_name(allocator,
                                                      data_table_id,
                                                      index_name,
                                                      index_table_name))) {
    } else {
      const bool is_index = true;
      ObTableSchema new_index_table_schema;
      if (OB_FAIL(schema_guard.get_table_schema(
                                                database_id,
                                                index_table_name,
                                                is_index,
                                                index_table_schema))) {
      } else if (OB_UNLIKELY(NULL == index_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        RS_LOG(WARN, "get index table schema failed",
               K(database_id), K(index_table_name), K(ret));
      } else if (index_table_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        RS_LOG(WARN, "index table is in recyclebin", K(ret));
      } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(new_index_table_schema.assign(*index_table_schema))) {
      } else {
        new_index_table_schema.set_index_visibility(alter_index_arg.index_visibility_);
        new_index_table_schema.set_schema_version(new_schema_version);
        if(OB_FAIL(schema_service->get_table_sql_service().update_table_options(
                    trans,
                    *index_table_schema,
                    new_index_table_schema,
                    index_table_schema->is_global_index_table() ? OB_DDL_ALTER_GLOBAL_INDEX: OB_DDL_ALTER_TABLE))) {
        }
      }
    }
    RS_LOG(INFO, "finish alter table alter index", K(alter_index_arg), K(ret));
  }
  return ret;
}

// description: delete foreign key of table in a transaction
//
// @param [in] table_schema
// @param [in] drop_foreign_key_arg
// @param [in] trans
//
// @return oceanbase error code defined in lib/ob_errno.def
int ObDDLOperator::alter_table_drop_foreign_key(const ObTableSchema &table_schema,
                                                const ObDropForeignKeyArg &drop_foreign_key_arg,
                                                ObMySQLTransaction &trans,
                                                const ObForeignKeyInfo *&parent_table_mock_foreign_key_info,
                                                const bool parent_table_in_offline_ddl_white_list)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  ObTableSqlService *table_sql_service = NULL;
  const ObString &foreign_key_name = drop_foreign_key_arg.foreign_key_name_;
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service_impl must not null", K(ret));
  } else if (FALSE_IT(table_sql_service = &schema_service_impl->get_table_sql_service())) {
  } else {
    const ObIArray<ObForeignKeyInfo> &foreign_key_infos = table_schema.get_foreign_key_infos();
    const ObForeignKeyInfo *foreign_key_info = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < foreign_key_infos.count(); i++) {
      if (0 == foreign_key_name.case_compare(foreign_key_infos.at(i).foreign_key_name_)
          && table_schema.get_table_id() == foreign_key_infos.at(i).child_table_id_) {
        foreign_key_info = &foreign_key_infos.at(i);
        break;
      }
    }
    if (OB_SUCC(ret) && OB_ISNULL(foreign_key_info)) {
      ret = OB_ERR_CANT_DROP_FIELD_OR_KEY;
      LOG_USER_ERROR(OB_ERR_CANT_DROP_FIELD_OR_KEY, foreign_key_name.length(), foreign_key_name.ptr());
      LOG_WARN("Cannot drop foreign key constraint  - nonexistent constraint", K(ret), K(foreign_key_name), K(table_schema.get_table_name_str()));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else if (OB_FAIL(table_sql_service->drop_foreign_key(
                       new_schema_version, trans, table_schema, foreign_key_info, parent_table_in_offline_ddl_white_list))) {
    } else if (nullptr != foreign_key_info && foreign_key_info->is_parent_table_mock_) {
      parent_table_mock_foreign_key_info = foreign_key_info;
    }
  }
  return ret;
}

int ObDDLOperator::create_mock_fk_parent_table(
    ObMySQLTransaction &trans,
    const share::schema::ObMockFKParentTableSchema &mock_fk_parent_table_schema,
    const bool need_update_foreign_key)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  if (OB_FAIL(schema_service_impl->get_table_sql_service().add_mock_fk_parent_table(
              &trans, mock_fk_parent_table_schema, need_update_foreign_key))) {
  } else if (need_update_foreign_key) { // if need_update_foreign_key, then need_sync_version_for_cascade_child_table
    ObArray<uint64_t> child_table_ids;
    for (int64_t i = 0; OB_SUCC(ret) && i < mock_fk_parent_table_schema.get_foreign_key_infos().count(); ++i) {
      if (OB_FAIL(child_table_ids.push_back(mock_fk_parent_table_schema.get_foreign_key_infos().at(i).child_table_id_))) {
      }
    }
    if (FAILEDx(sync_version_for_cascade_table(child_table_ids, trans))) {
      LOG_WARN("fail to sync versin for children tables", K(ret), K(child_table_ids));
    }
  }
  return ret;
}

int ObDDLOperator::alter_mock_fk_parent_table(
    ObMySQLTransaction &trans,
    share::schema::ObMockFKParentTableSchema &mock_fk_parent_table_schema)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  if (OB_FAIL(schema_service_impl->get_table_sql_service().alter_mock_fk_parent_table(
              &trans, mock_fk_parent_table_schema))) {
  }
  return ret;
}

int ObDDLOperator::drop_mock_fk_parent_table(
    ObMySQLTransaction &trans,
    const share::schema::ObMockFKParentTableSchema &mock_fk_parent_table_schema)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  if (OB_FAIL(schema_service_impl->get_table_sql_service().drop_mock_fk_parent_table(
              &trans, mock_fk_parent_table_schema))) {
  }
  return ret;
}

int ObDDLOperator::replace_mock_fk_parent_table(
    ObMySQLTransaction &trans,
    share::schema::ObSchemaGetterGuard &schema_guard,
    const share::schema::ObMockFKParentTableSchema &mock_fk_parent_table_schema)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  const ObMockFKParentTableSchema *ori_mock_fk_parent_table_schema_ptr = NULL;
  if (OB_FAIL(schema_guard.get_mock_fk_parent_table_schema_with_id(mock_fk_parent_table_schema.get_mock_fk_parent_table_id(),
      ori_mock_fk_parent_table_schema_ptr))) {
  } else if (OB_ISNULL(ori_mock_fk_parent_table_schema_ptr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ori_mock_fk_parent_table_schema_ptr is null", K(ret), KPC(ori_mock_fk_parent_table_schema_ptr), K(mock_fk_parent_table_schema));
  } else if (mock_fk_parent_table_schema.get_foreign_key_infos().count() <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("count of foreign_key_infos in mock_fk_parent_table_schema is zero", K(ret), KPC(ori_mock_fk_parent_table_schema_ptr), K(mock_fk_parent_table_schema.get_foreign_key_infos().count()));
  } else if (OB_FAIL(schema_service_impl->get_table_sql_service().replace_mock_fk_parent_table(
                     &trans, mock_fk_parent_table_schema, ori_mock_fk_parent_table_schema_ptr))) {
  } else { // update schema version of child tables and new parent table after replace mock_fk_parent_table with new parent table
    ObArray<uint64_t> child_table_ids;
    uint64_t new_parent_table_id = mock_fk_parent_table_schema.get_foreign_key_infos().at(0).parent_table_id_;
    for (int64_t i = 0; OB_SUCC(ret) && i < mock_fk_parent_table_schema.get_foreign_key_infos().count(); ++i) {
      if (OB_FAIL(child_table_ids.push_back(mock_fk_parent_table_schema.get_foreign_key_infos().at(i).child_table_id_))) {
      }
    }
    if (FAILEDx(sync_version_for_cascade_table(child_table_ids, trans))) {
      LOG_WARN("fail to sync versin for children tables", K(ret), K(child_table_ids));
    }
    if (FAILEDx(schema_service_impl->get_table_sql_service().update_data_table_schema_version(trans, new_parent_table_id, false))) {
      LOG_WARN("failed to update parent table schema version", K(ret), K(mock_fk_parent_table_schema.get_foreign_key_infos().at(0)));
    }
  }
  return ret;
}

int ObDDLOperator::sync_version_for_cascade_mock_fk_parent_table(
    const common::ObIArray<uint64_t> &table_ids,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  uint64_t id = OB_INVALID_ID;
  const ObMockFKParentTableSchema *schema = NULL;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    RS_LOG(ERROR, "schema_service must not null");
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < table_ids.count() ; ++i) {
      ObSchemaGetterGuard schema_guard;
      id = table_ids.at(i);
      ObMockFKParentTableSchema tmp_schema;
      if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_mock_fk_parent_table_schema_with_id(id, schema))) {
      } else if (!schema) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema is NULL", K(ret));
      } else if (OB_FAIL(tmp_schema.assign(*schema))) {
      } else if (OB_FAIL(schema_service->get_table_sql_service().update_mock_fk_parent_table_schema_version(
              &trans,
              tmp_schema))) {
      }
    }
  }

  return ret;
}

int ObDDLOperator::deal_with_mock_fk_parent_table(
    ObMySQLTransaction &trans,
    share::schema::ObSchemaGetterGuard &schema_guard,
    ObMockFKParentTableSchema &mock_fk_parent_table_schema)
{
  int ret = OB_SUCCESS;
  int64_t new_schema_version = OB_INVALID_VERSION;
  if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (FALSE_IT(mock_fk_parent_table_schema.set_schema_version(new_schema_version))) {
  } else if (MOCK_FK_PARENT_TABLE_OP_CREATE_TABLE_BY_DROP_PARENT_TABLE == mock_fk_parent_table_schema.get_operation_type()) {
    // One scenes :
    // 1. dropped real parent table
    if (OB_FAIL(create_mock_fk_parent_table(trans, mock_fk_parent_table_schema, true))) {
    }
  } else if (MOCK_FK_PARENT_TABLE_OP_CREATE_TABLE_BY_ADD_FK_IN_CHILD_TBALE == mock_fk_parent_table_schema.get_operation_type()) {
    // Two scenes :
    // 1. create child table with a fk references a mock fk parent table
    // 2. alter child table add fk references a mock fk parent table
    if (OB_FAIL(create_mock_fk_parent_table(trans, mock_fk_parent_table_schema, false))) {
    }
  } else if (MOCK_FK_PARENT_TABLE_OP_DROP_TABLE == mock_fk_parent_table_schema.get_operation_type()) {
    // Three scenes :
    // 1. drop child table with a fk references a mock fk parent table existed
    // 2. drop fk from a child table with a fk references a mock fk parent table existed
    // 3. drop database
    if (OB_FAIL(drop_mock_fk_parent_table(trans, mock_fk_parent_table_schema))) {
    }
  } else if (MOCK_FK_PARENT_TABLE_OP_ADD_COLUMN == mock_fk_parent_table_schema.get_operation_type()
             || MOCK_FK_PARENT_TABLE_OP_DROP_COLUMN == mock_fk_parent_table_schema.get_operation_type()
             || MOCK_FK_PARENT_TABLE_OP_UPDATE_SCHEMA_VERSION == mock_fk_parent_table_schema.get_operation_type()) {
    // Three scenes :
    // 1. create child table with a fk references a mock fk parent table existed
    // 2. alter child table add fk references a mock fk parent table existed
    // 3. drop fk from a child table with a fk references a mock fk parent table existed
    if (OB_FAIL(alter_mock_fk_parent_table(trans, mock_fk_parent_table_schema))) {
    }
  } else if (MOCK_FK_PARENT_TABLE_OP_REPLACED_BY_REAL_PREANT_TABLE == mock_fk_parent_table_schema.get_operation_type()) {
    // Five scenes :
    // 1. create table (as select)
    // 2. create table like
    // 3. rename table
    // 4. alter table rename to
    // 5. restore table from recyclebin
    if (OB_FAIL(replace_mock_fk_parent_table(trans, schema_guard, mock_fk_parent_table_schema))) {
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("operation_type is INVALID", K(ret), K(mock_fk_parent_table_schema.get_operation_type()), K(mock_fk_parent_table_schema), K(lbt()));
  }
  return ret;
}

int ObDDLOperator::deal_with_mock_fk_parent_tables(
    ObMySQLTransaction &trans,
    share::schema::ObSchemaGetterGuard &schema_guard,
    ObIArray<ObMockFKParentTableSchema> &mock_fk_parent_table_schema_array)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < mock_fk_parent_table_schema_array.count(); ++i) {
    if (OB_FAIL(deal_with_mock_fk_parent_table(trans, schema_guard, mock_fk_parent_table_schema_array.at(i)))) {
    }
  }
  return ret;
}

int ObDDLOperator::alter_index_drop_options(const ObTableSchema &index_table_schema,
                                            const ObString &table_name,
                                            ObTableSchema &new_index_table_schema,
                                            ObMySQLTransaction &trans) {
  int ret = OB_SUCCESS;
  const int INVISIBLE = 1;
  const uint64_t DROPINDEX = 1;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (!index_table_schema.is_index_table()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("index_table_schema is not index", K(ret));
  } else if (OB_FAIL(new_index_table_schema.assign(index_table_schema))) {
  } else {
    uint64_t INVISIBLEBEFORE = 0;
    if (!new_index_table_schema.is_index_visible()) {
      INVISIBLEBEFORE = 1;
    } else {
      INVISIBLEBEFORE = 0;
      new_index_table_schema.set_index_visibility(INVISIBLE);
    }
    new_index_table_schema.set_invisible_before(INVISIBLEBEFORE);
    new_index_table_schema.set_drop_index(DROPINDEX);

    ObSqlString sql;
    ObString index_name;
    if (OB_FAIL(ObTableSchema::get_index_name(allocator,
            index_table_schema.get_data_table_id(),
            index_table_schema.get_table_name_str(),
            index_name))) {
    } else if (OB_FAIL(sql.append_fmt("DROP INDEX %.*s on %.*s",
            index_name.length(),
            index_name.ptr(),
            table_name.length(),
            table_name.ptr()))) {
    } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else {
      ObString ddl_stmt_str = sql.string();
      new_index_table_schema.set_schema_version(new_schema_version);
      if (OB_FAIL(schema_service->get_table_sql_service().update_table_options(
              trans,
              index_table_schema,
              new_index_table_schema,
              OB_DDL_DROP_INDEX_TO_RECYCLEBIN,
              &ddl_stmt_str))) {
      }
    }
  }
  return ret;
}

int ObDDLOperator::alter_table_rename_index(
    const uint64_t data_table_id,
    const uint64_t database_id,
    const obcall::ObRenameIndexArg &rename_index_arg,
    const ObIndexStatus *new_index_status,
    const bool is_in_deleting,
    common::ObMySQLTransaction &trans,
    schema::ObTableSchema &new_index_table_schema)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", KR(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    RS_LOG(INFO, "start alter table rename index", K(rename_index_arg));
    const ObTableSchema *index_table_schema = nullptr;
    ObString index_table_name;
    ObString new_index_table_name;
    ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
    const ObString &index_name = rename_index_arg.origin_index_name_;
    const ObString &new_index_name = rename_index_arg.new_index_name_;

    if (OB_FAIL(ObTableSchema::build_index_table_name(allocator,
                                                      data_table_id,
                                                      index_name,
                                                      index_table_name))) {
    } else if (OB_FAIL(ObTableSchema::build_index_table_name(allocator,
                                                      data_table_id,
                                                      new_index_name,
                                                      new_index_table_name))) {
    } else {
      const bool is_index = true;
      if (OB_FAIL(schema_guard.get_table_schema(
                                                database_id,
                                                index_table_name,
                                                is_index,
                                                index_table_schema))) {
      } else if (OB_ISNULL(index_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("index table schema is NULL", KR(ret), K(database_id), K(index_table_name));
      } else if (OB_FAIL(inner_alter_table_rename_index_(index_table_schema, new_index_table_name,
              new_index_status, is_in_deleting, trans, new_index_table_schema))) {
      } else if (is_fts_index_aux(index_table_schema->get_index_type())) {
        if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                       database_id,
                                                       INDEX_TYPE_FTS_DOC_WORD_LOCAL, /* index_type */
                                                       index_name,
                                                       new_index_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       schema_guard,
                                                       trans,
                                                       allocator))) {
        }
      } else if (is_vec_delta_buffer_type(index_table_schema->get_index_type())) {
        if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                       database_id,
                                                       INDEX_TYPE_VEC_INDEX_ID_LOCAL, /* index_type */
                                                       index_name,
                                                       new_index_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       schema_guard,
                                                       trans,
                                                       allocator))) {
        } else if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                       database_id,
                                                       INDEX_TYPE_VEC_INDEX_SNAPSHOT_DATA_LOCAL, /* index_type */
                                                       index_name,
                                                       new_index_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       schema_guard,
                                                       trans,
                                                       allocator))) {
        }
      } else if (is_hybrid_vec_index_log_type(index_table_schema->get_index_type())) {
        if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                       database_id,
                                                       INDEX_TYPE_VEC_INDEX_ID_LOCAL, /* index_type */
                                                       index_name,
                                                       new_index_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       schema_guard,
                                                       trans,
                                                       allocator))) {
        } else if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                       database_id,
                                                       INDEX_TYPE_VEC_INDEX_SNAPSHOT_DATA_LOCAL, /* index_type */
                                                       index_name,
                                                       new_index_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       schema_guard,
                                                       trans,
                                                       allocator))) {
        } else if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                       database_id,
                                                       INDEX_TYPE_HYBRID_INDEX_EMBEDDED_LOCAL, /* index_type */
                                                       index_name,
                                                       new_index_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       schema_guard,
                                                       trans,
                                                       allocator))) {
        }
      } else if (is_vec_ivfflat_centroid_index(index_table_schema->get_index_type())) {
        if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                       database_id,
                                                       INDEX_TYPE_VEC_IVFFLAT_CID_VECTOR_LOCAL, /* index_type */
                                                       index_name,
                                                       new_index_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       schema_guard,
                                                       trans,
                                                       allocator))) {
        } else if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                              database_id,
                                                              INDEX_TYPE_VEC_IVFFLAT_ROWKEY_CID_LOCAL, /* index_type */
                                                              index_name,
                                                              new_index_name,
                                                              new_index_status,
                                                              is_in_deleting,
                                                              schema_guard,
                                                              trans,
                                                              allocator))) {
        }
      } else if (is_vec_ivfsq8_centroid_index(index_table_schema->get_index_type())) {
         if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                       database_id,
                                                       INDEX_TYPE_VEC_IVFSQ8_META_LOCAL, /* index_type */
                                                       index_name,
                                                       new_index_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       schema_guard,
                                                       trans,
                                                       allocator))) {
        } else if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                              database_id,
                                                              INDEX_TYPE_VEC_IVFSQ8_CID_VECTOR_LOCAL, /* index_type */
                                                              index_name,
                                                              new_index_name,
                                                              new_index_status,
                                                              is_in_deleting,
                                                              schema_guard,
                                                              trans,
                                                              allocator))) {
        } else if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                              database_id,
                                                              INDEX_TYPE_VEC_IVFSQ8_ROWKEY_CID_LOCAL, /* index_type */
                                                              index_name,
                                                              new_index_name,
                                                              new_index_status,
                                                              is_in_deleting,
                                                              schema_guard,
                                                              trans,
                                                              allocator))) {
        }
      } else if (is_vec_ivfpq_centroid_index(index_table_schema->get_index_type())) {
         if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                       database_id,
                                                       INDEX_TYPE_VEC_IVFPQ_PQ_CENTROID_LOCAL, /* index_type */
                                                       index_name,
                                                       new_index_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       schema_guard,
                                                       trans,
                                                       allocator))) {
        } else if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                              database_id,
                                                              INDEX_TYPE_VEC_IVFPQ_CODE_LOCAL, /* index_type */
                                                              index_name,
                                                              new_index_name,
                                                              new_index_status,
                                                              is_in_deleting,
                                                              schema_guard,
                                                              trans,
                                                              allocator))) {
        } else if (OB_FAIL(alter_table_rename_built_in_index_(data_table_id,
                                                              database_id,
                                                              INDEX_TYPE_VEC_IVFPQ_ROWKEY_CID_LOCAL, /* index_type */
                                                              index_name,
                                                              new_index_name,
                                                              new_index_status,
                                                              is_in_deleting,
                                                              schema_guard,
                                                              trans,
                                                              allocator))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::alter_table_rename_built_in_index_(const uint64_t data_table_id,
    const uint64_t database_id,
    const ObIndexType index_type,
    const ObString &index_name,
    const ObString &new_index_name,
    const ObIndexStatus *new_index_status,
    const bool is_in_deleting,
    share::schema::ObSchemaGetterGuard &schema_guard,
    common::ObMySQLTransaction &trans,
    ObArenaAllocator &allocator)
{
  int ret = OB_SUCCESS;
  SMART_VARS_3((ObTableSchema, new_table_schema),
               (obcall::ObCreateIndexArg, origin_index_arg),
               (obcall::ObCreateIndexArg, new_index_arg)) {
    const ObTableSchema *origin_table_schema = NULL;
    origin_index_arg.index_name_ = index_name;
    origin_index_arg.index_type_ = index_type;
    new_index_arg.index_name_ = new_index_name;
    new_index_arg.index_type_ = index_type;
    ObString origin_index_table_name;
    ObString new_index_table_name;
    if (is_fts_index(index_type)) { // fts index
      if (OB_FAIL(ObFtsIndexBuilderUtil::generate_fts_aux_index_name(origin_index_arg, &allocator))) {
      } else if (OB_FAIL(ObFtsIndexBuilderUtil::generate_fts_aux_index_name(new_index_arg, &allocator))) {
      }
    } else if (is_vec_index(index_type)) {  // vector index
      if (OB_FAIL(ObVecIndexBuilderUtil::generate_vec_index_name(&allocator,
                                                                 index_type,
                                                                 index_name,
                                                                 origin_index_arg.index_name_))) {
      } else if (OB_FAIL(ObVecIndexBuilderUtil::generate_vec_index_name(&allocator,
                                                                        index_type,
                                                                        new_index_name,
                                                                        new_index_arg.index_name_))) {
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected index type", K(ret), K(index_type));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObTableSchema::build_index_table_name(allocator,
                                                             data_table_id,
                                                             origin_index_arg.index_name_,
                                                             origin_index_table_name))) {
    } else if (OB_FAIL(ObTableSchema::build_index_table_name(allocator,
                                                             data_table_id,
                                                             new_index_arg.index_name_,
                                                             new_index_table_name))) {
    } else if (OB_FAIL(schema_guard.get_table_schema(
                                                     database_id,
                                                     origin_index_table_name,
                                                     true/*is_index*/,
                                                     origin_table_schema,
                                                     false/*is_hidden*/,
                                                     true/*is_built_in_index*/))) {
    } else if (OB_ISNULL(origin_table_schema)) {
      ret = OB_EAGAIN;
      LOG_WARN("the domain index may be being built",
          K(ret), K(origin_index_table_name));
    } else if (OB_FAIL(inner_alter_table_rename_index_(origin_table_schema,
                                                       new_index_table_name,
                                                       new_index_status,
                                                       is_in_deleting,
                                                       trans,
                                                       new_table_schema))) {
    }
  }
  return ret;
}

int ObDDLOperator::alter_table_rename_index_with_origin_index_name(const uint64_t index_table_id,
    const ObString &new_index_name, // Attention!!! origin index name, don't use table name. For example, __idx_500005_{index_name}, please using index_name!!!
    const ObIndexStatus &new_index_status,
    const bool is_in_deleting,
    common::ObMySQLTransaction &trans,
    share::schema::ObTableSchema &new_index_table_schema)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
  ObString new_index_table_name;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *index_table_schema = nullptr;
  RS_LOG(INFO, "start alter table rename index", K(index_table_id), K(new_index_name));
  if (OB_UNLIKELY(OB_INVALID_ID == index_table_id || new_index_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(index_table_id), K(new_index_name));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( index_table_id, index_table_schema))) {
  } else if (OB_ISNULL(index_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unecpected error, index table schema is nullptr", K(ret), K(index_table_id));
  } else if (OB_FAIL(ObTableSchema::build_index_table_name(allocator,
                                                           index_table_schema->get_data_table_id(),
                                                           new_index_name,
                                                           new_index_table_name))) {
  } else if (OB_FAIL(inner_alter_table_rename_index_(index_table_schema, new_index_table_name, &new_index_status,
             is_in_deleting, trans, new_index_table_schema))) {
  }
  return ret;
}

int ObDDLOperator::inner_alter_table_rename_index_(const share::schema::ObTableSchema *index_table_schema,
    const ObString &new_index_name,
    const ObIndexStatus *new_index_status,
    const bool is_in_deleting,
    common::ObMySQLTransaction &trans,
    share::schema::ObTableSchema &new_index_table_schema)
{
  int ret = OB_SUCCESS;
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  const bool in_offline_ddl_white_list = new_index_table_schema.get_in_offline_ddl_white_list();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_ISNULL(index_table_schema)
          || OB_UNLIKELY(new_index_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(index_table_schema), KP(new_index_status), K(new_index_name));
  } else if (index_table_schema->is_in_recyclebin()) {
    ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    LOG_WARN("index table is in recyclebin", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(new_index_table_schema.assign(*index_table_schema))) {
  } else {
    new_index_table_schema.set_schema_version(new_schema_version);
    if (nullptr != new_index_status) {
      new_index_table_schema.set_index_status(*new_index_status);
    }
    new_index_table_schema.set_is_in_deleting(is_in_deleting);
    new_index_table_schema.set_name_generated_type(GENERATED_TYPE_USER);
    new_index_table_schema.set_in_offline_ddl_white_list(in_offline_ddl_white_list || new_index_table_schema.get_in_offline_ddl_white_list());
    if (OB_FAIL(new_index_table_schema.set_table_name(new_index_name))) {
    } else if (OB_FAIL(schema_service->get_table_sql_service().update_table_options(
                trans,
                *index_table_schema,
                new_index_table_schema,
                index_table_schema->is_global_index_table() ? OB_DDL_RENAME_GLOBAL_INDEX: OB_DDL_RENAME_INDEX))) {
    }
  }
  return ret;
}

int ObDDLOperator::alter_index_table_parallel(
    const uint64_t data_table_id,
    const uint64_t database_id,
    const obcall::ObAlterIndexParallelArg &alter_parallel_arg,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    int64_t new_schema_version = OB_INVALID_VERSION;
    RS_LOG(INFO, "start alter table alter index parallel", K(alter_parallel_arg));
    const ObTableSchema *index_table_schema = NULL;
    ObString index_table_name;
    ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
    const ObString &index_name = alter_parallel_arg.index_name_;

    if (OB_FAIL(ObTableSchema::build_index_table_name(allocator,
                                                      data_table_id,
                                                      index_name,
                                                      index_table_name))) {
    } else {
      const bool is_index = true;
      if (OB_FAIL(schema_guard.get_table_schema(
                                                database_id,
                                                index_table_name,
                                                is_index,
                                                index_table_schema))) {
      } else if (OB_UNLIKELY(NULL == index_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        RS_LOG(WARN, "get index table schema failed",
          K(database_id), K(index_table_name), K(ret));
      } else if (index_table_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("index table is in recyclebin", K(ret));
      } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else {
        ObTableSchema new_index_table_schema;
        if (OB_FAIL(new_index_table_schema.assign(*index_table_schema))) {
        } else {
          new_index_table_schema.set_schema_version(new_schema_version);
        }
        if (OB_SUCC(ret)) {
          new_index_table_schema.set_dop(alter_parallel_arg.new_parallel_);
          if (OB_FAIL(schema_service->get_table_sql_service().update_table_options(
                    trans,
                    *index_table_schema,
                    new_index_table_schema,
                    OB_DDL_ALTER_INDEX_PARALLEL))) {
          }
        }
      }
    }
  }
  return ret;
}

//hualong delete later
//int ObDDLOperator::log_ddl_operation(ObSchemaOperation &ddl_operation,
//                                     ObMySQLTransaction &trans)
//{
//  int ret = OB_SUCCESS;
//  ObSchemaService *schema_service = schema_service_.get_schema_service();
//  if (OB_ISNULL(schema_service)) {
//    ret = OB_ERR_UNEXPECTED;
//    LOG_WARN("schema_service is NULL", K(ret));
//  } else if (OB_FAIL(schema_service->log_operation(ddl_operation, &trans))) {
//    RS_LOG(WARN, "failed to log ddl operation!", K(ret));
//  } else {
//    // do-nothing
//  }
//  return ret;
//}

int ObDDLOperator::alter_table_options(
    ObSchemaGetterGuard &schema_guard,
    ObTableSchema &new_table_schema,
    const ObTableSchema &table_schema,
    const bool need_update_aux_table,
    ObMySQLTransaction &trans,
    const ObIArray<ObTableSchema> *global_idx_schema_array/*=NULL*/,
    common::ObIArray<std::pair<uint64_t, int64_t>> *idx_schema_versions /*=NULL*/) // pair : <table_id, schema_version>
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    RS_LOG(WARN, "schema sql service must not be null",
           K(schema_service), K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    new_table_schema.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_table_sql_service().update_table_options(
        trans,
        table_schema,
        new_table_schema,
        OB_DDL_ALTER_TABLE))) {
    } else if (need_update_aux_table) {
      bool has_aux_table_updated = false;
      if (nullptr != idx_schema_versions) {
        idx_schema_versions->reset();
      }
      if (OB_FAIL(update_aux_table(table_schema,
          new_table_schema,
          schema_guard,
          trans,
          USER_INDEX,
          has_aux_table_updated,
          global_idx_schema_array,
          idx_schema_versions))) {
      } else if (OB_FAIL(update_aux_table(table_schema,
          new_table_schema,
          schema_guard,
          trans,
          AUX_LOB_META,
          has_aux_table_updated,
          NULL,
          idx_schema_versions))) {
      } else if (OB_FAIL(update_aux_table(table_schema,
          new_table_schema,
          schema_guard,
          trans,
          AUX_LOB_PIECE,
          has_aux_table_updated,
          NULL,
          idx_schema_versions))) {
      }

      if (OB_SUCC(ret) && has_aux_table_updated) {
        // update data table schema version
        if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_service->get_table_sql_service().update_data_table_schema_version(trans,
                    new_table_schema.get_table_id(), table_schema.get_in_offline_ddl_white_list(), new_schema_version))) {
        } else {
          new_table_schema.set_schema_version(new_schema_version);
        }
      }
    } // need_update_aux_table
  }
  return ret;
}

/*
 * the input value of has_aux_table_updated maybe true or false.
 * has_aux_table_updated represents that if any aux_table updated schema version,
 * aux_table including index table(s), lob meta table, lob piece table.
*/
int ObDDLOperator::update_aux_table(
    const ObTableSchema &table_schema,
    const ObTableSchema &new_table_schema,
    ObSchemaGetterGuard &schema_guard,
    ObMySQLTransaction &trans,
    const ObTableType table_type,
    bool &has_aux_table_updated, /*OUTPUT*/
    const ObIArray<ObTableSchema> *global_idx_schema_array/*=NULL*/,
    common::ObIArray<std::pair<uint64_t, int64_t>> *idx_schema_versions /*=NULL*/) // pair : <table_id, schema_version>

{
  int ret = OB_SUCCESS;

  const bool is_index = USER_INDEX == table_type;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
  uint64_t lob_meta_table_id = OB_INVALID_ID;
  uint64_t lob_piece_table_id = OB_INVALID_ID;
  int64_t N = 0;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    RS_LOG(WARN, "schema sql service must not be null",
           K(schema_service), K(ret));
  } else {
    if (table_type == USER_INDEX) {
      if (OB_FAIL(new_table_schema.get_simple_index_infos(simple_index_infos))) {
      } else {
        N = simple_index_infos.count();
      }
    } else if (table_type == AUX_LOB_META) {
      lob_meta_table_id = new_table_schema.get_aux_lob_meta_tid();
      N = (table_schema.has_lob_aux_table() && new_table_schema.has_lob_aux_table()) ? 1 : 0;
    } else if (table_type == AUX_LOB_PIECE) {
      lob_piece_table_id = new_table_schema.get_aux_lob_piece_tid();
      N = (table_schema.has_lob_aux_table() && new_table_schema.has_lob_aux_table()) ? 1 : 0;
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid table type", K(ret), K(table_type));
    }
  }
  if (OB_SUCC(ret)) {
    ObTableSchema new_aux_table_schema;
    for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
      const ObTableSchema *aux_table_schema = NULL;
      if (is_index && OB_NOT_NULL(global_idx_schema_array) && !global_idx_schema_array->empty()) {
        for (int64_t j = 0; OB_SUCC(ret) && j < global_idx_schema_array->count(); ++j) {
          if (simple_index_infos.at(i).table_id_ == global_idx_schema_array->at(j).get_table_id()) {
            aux_table_schema = &(global_idx_schema_array->at(j));
            break;
          }
        }
      }
      uint64_t tid = 0;
      if (table_type == USER_INDEX) {
        tid = simple_index_infos.at(i).table_id_;
      } else if (table_type == AUX_LOB_META) {
        tid = lob_meta_table_id;
      } else if (table_type == AUX_LOB_PIECE) {
        tid = lob_piece_table_id;
      }
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(aux_table_schema)
                 && OB_FAIL(schema_guard.get_table_schema( tid, aux_table_schema))) {
        RS_LOG(WARN, "get_table_schema failed", "table id", tid, K(ret));
      } else if (OB_ISNULL(aux_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        RS_LOG(WARN, "table schema should not be null", K(ret));
      } else {
        new_aux_table_schema.reset();
        if (OB_FAIL(new_aux_table_schema.assign(*aux_table_schema))) {
        } else {
          new_aux_table_schema.set_database_id(new_table_schema.get_database_id());
          new_aux_table_schema.set_read_only(new_table_schema.is_read_only());
          new_aux_table_schema.set_progressive_merge_num(new_table_schema.get_progressive_merge_num());
          new_aux_table_schema.set_tablet_size(new_table_schema.get_tablet_size());
          new_aux_table_schema.set_pctfree(new_table_schema.get_pctfree());
          new_aux_table_schema.set_block_size(new_table_schema.get_block_size());
          new_aux_table_schema.set_row_store_type(new_table_schema.get_row_store_type());
          new_aux_table_schema.set_store_format(new_table_schema.get_store_format());
          new_aux_table_schema.set_progressive_merge_round(new_table_schema.get_progressive_merge_round());
          // index table should only inherit table mode and table state flag from data table
          new_aux_table_schema.set_table_mode(new_table_schema.get_table_mode_flag());
          new_aux_table_schema.set_table_state_flag(new_table_schema.get_table_state_flag());
          new_aux_table_schema.set_lob_inrow_threshold(new_table_schema.get_lob_inrow_threshold());
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(new_aux_table_schema.set_compress_func_name(new_table_schema.get_compress_func_name()))) {
        } else if (aux_table_schema->is_in_recyclebin()) {

          ObArray<ObRecycleObject> recycle_objs;
          ObRecycleObject::RecycleObjType recycle_type = ObRecycleObject::get_type_by_table_schema(*aux_table_schema);
          new_aux_table_schema.set_database_id(aux_table_schema->get_database_id());
          if (OB_FAIL(schema_service->fetch_recycle_object(aux_table_schema->get_table_name_str(),
                  recycle_type,
                  trans,
                  recycle_objs))) {
          } else if (recycle_objs.size() != 1) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected recycle object num", K(ret), K(*aux_table_schema), "size", recycle_objs.size());
          } else if (OB_FAIL(schema_service->delete_recycle_object(recycle_objs.at(0),
                  trans))) {
          } else {
            ObRecycleObject &recycle_obj = recycle_objs.at(0);
            recycle_obj.set_database_id(new_table_schema.get_database_id());
            if (OB_FAIL(schema_service->insert_recyclebin_object(recycle_obj, trans))) {
            }
          }
        }
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else {
          has_aux_table_updated = true;
          new_aux_table_schema.set_schema_version(new_schema_version);
          if (OB_FAIL(schema_service->get_table_sql_service().only_update_table_options(
                  trans,
                  new_aux_table_schema,
                  OB_DDL_ALTER_TABLE))) {
          } else if ((nullptr != idx_schema_versions) &&
              OB_FAIL(idx_schema_versions->push_back(std::make_pair(new_aux_table_schema.get_table_id(), new_schema_version)))) {
            RS_LOG(WARN, "fail to push_back array", K(ret), KPC(idx_schema_versions), K(new_schema_version));
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::rename_table(const ObTableSchema &table_schema,
                                const ObString &new_table_name,
                                const uint64_t new_db_id,
                                const bool need_reset_object_status,
                                ObMySQLTransaction &trans,
                                const ObString *ddl_stmt_str,
                                int64_t &new_data_table_schema_version /*OUTPUT*/,
                                ObIArray<std::pair<uint64_t, int64_t>> &idx_schema_versions /*OUTPUT*/) // pair : table_id, schema_version
{
  int ret = OB_SUCCESS;
  idx_schema_versions.reset();

  new_data_table_schema_version = OB_INVALID_VERSION;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    RS_LOG(WARN, "schema sql service must not be null",
           K(schema_service), K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_data_table_schema_version))) {
  } else {
    ObTableSchema new_table_schema;
    if (OB_FAIL(new_table_schema.assign(table_schema))) {
    } else {
      new_table_schema.set_schema_version(new_data_table_schema_version);
    }
    if (need_reset_object_status) {
      new_table_schema.set_object_status(ObObjectStatus::INVALID);
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(new_table_schema.set_table_name(new_table_name))) {
    } else {
      new_table_schema.set_database_id(new_db_id);
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_service->get_table_sql_service().update_table_options(
          trans,
          table_schema,
          new_table_schema,
          OB_DDL_TABLE_RENAME,
          ddl_stmt_str))) {
      } else {
        bool has_aux_table_updated = false;
        HEAP_VAR(ObTableSchema, new_aux_table_schema) {
          { // update index table
            ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
            if (OB_FAIL(table_schema.get_simple_index_infos(simple_index_infos))) {
            } else {
              for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
                if (OB_FAIL(rename_aux_table(new_table_schema,
                                             simple_index_infos.at(i).table_id_,
                                             schema_guard,
                                             trans,
                                             new_aux_table_schema,
                                             has_aux_table_updated))) {
                } else if (OB_FAIL(idx_schema_versions.push_back(std::make_pair(new_aux_table_schema.get_table_id(), new_aux_table_schema.get_schema_version())))) {
                }
              }
            }
          }
          if (OB_SUCC(ret) && table_schema.has_lob_aux_table()) {
            uint64_t mtid = table_schema.get_aux_lob_meta_tid();
            uint64_t ptid = table_schema.get_aux_lob_piece_tid();
            if (OB_INVALID_ID == mtid || OB_INVALID_ID == ptid) {
              ret = OB_ERR_UNEXPECTED;
              RS_LOG(WARN, "Expect meta tid and piece tid valid", KR(ret), K(mtid), K(ptid));
            } else if (OB_FAIL(rename_aux_table(new_table_schema,
                                                mtid,
                                                schema_guard,
                                                trans,
                                                new_aux_table_schema,
                                                has_aux_table_updated))) {
            } else if (OB_FAIL(idx_schema_versions.push_back(std::make_pair(new_aux_table_schema.get_table_id(), new_aux_table_schema.get_schema_version())))) {
            } else if (OB_FAIL(rename_aux_table(new_table_schema,
                                                ptid,
                                                schema_guard,
                                                trans,
                                                new_aux_table_schema,
                                                has_aux_table_updated))) {
            } else if (OB_FAIL(idx_schema_versions.push_back(std::make_pair(new_aux_table_schema.get_table_id(), new_aux_table_schema.get_schema_version())))) {
            }
          }
        }

        if (OB_SUCC(ret) && has_aux_table_updated) {
          // update data table schema version
          if (OB_FAIL(schema_service_.gen_new_schema_version(new_data_table_schema_version))) {
          } else if (OB_FAIL(schema_service->get_table_sql_service().update_data_table_schema_version(trans,
                      new_table_schema.get_table_id(), table_schema.get_in_offline_ddl_white_list(), new_data_table_schema_version))) {
          }
        }
      }
    }
  }
  return ret;
}

/*
 * the input value of has_aux_table_updated maybe true or false.
 * has_aux_table_updated represents that if any aux_table updated schema version,
 * aux_table including index table(s), lob meta table, lob piece table.
*/
int ObDDLOperator::rename_aux_table(
    const ObTableSchema &new_table_schema,
    const uint64_t table_id,
    ObSchemaGetterGuard &schema_guard,
    ObMySQLTransaction &trans,
    ObTableSchema &new_aux_table_schema,
    bool &has_aux_table_updated /*OUTPUT*/)
{
  int ret = OB_SUCCESS;

  ObSchemaService *schema_service = schema_service_.get_schema_service();
  const ObTableSchema *aux_table_schema = NULL;
  int64_t new_schema_version = OB_INVALID_VERSION;
  if (OB_FAIL(schema_guard.get_table_schema( table_id, aux_table_schema))) {
  } else if (OB_ISNULL(aux_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    RS_LOG(WARN, "table schema should not be null", K(ret));
  } else {
    new_aux_table_schema.reset();
    if (OB_FAIL(new_aux_table_schema.assign(*aux_table_schema))) {
    } else {
      new_aux_table_schema.set_database_id(new_table_schema.get_database_id());
    }
    if (OB_FAIL(ret)) {
    } else if (aux_table_schema->is_in_recyclebin()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("aux table is in recycle bin while main table not in", K(ret), KPC(aux_table_schema));
    } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else {
      has_aux_table_updated = true;
      new_aux_table_schema.set_schema_version(new_schema_version);
      if (OB_FAIL(schema_service->get_table_sql_service().only_update_table_options(
              trans,
              new_aux_table_schema,
              OB_DDL_TABLE_RENAME))) {
      }
    }
  }
  return ret;
}

int ObDDLOperator::update_index_status(
    const uint64_t data_table_id,
    const uint64_t index_table_id,
    const share::schema::ObIndexStatus status,
    const bool in_offline_ddl_white_list,
    common::ObMySQLTransaction &trans,
    const common::ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *data_table_schema = nullptr;
  ObTableSchema copy_data_table_schema;

  if (OB_INVALID_ID == data_table_id || OB_INVALID_ID == index_table_id
      || status <= INDEX_STATUS_NOT_FOUND || status >= INDEX_STATUS_MAX) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(data_table_id), K(index_table_id), K(status));
  } else if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("schema service should not be NULL");
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( data_table_id, data_table_schema))) {
  } else if (nullptr == data_table_schema) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, table schema must not be nullptr", K(ret));
  } else if (OB_FAIL(copy_data_table_schema.assign(*data_table_schema))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (FALSE_IT(copy_data_table_schema.set_in_offline_ddl_white_list(in_offline_ddl_white_list))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().update_index_status(
      copy_data_table_schema, index_table_id, status, new_schema_version, trans, ddl_stmt_str))) {
  }
  return ret;
}

// "alter table ... partition by" clause needs to call this function to modify index type.
int ObDDLOperator::update_index_type(const ObTableSchema &data_table_schema,
                                     const uint64_t index_table_id,
                                     const share::schema::ObIndexType index_type,
                                     const common::ObString *ddl_stmt_str,
                                     common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObArray<uint64_t> index_table_ids;
  ObArray<ObIndexType> index_types;

  if (!data_table_schema.is_valid() ||
      OB_INVALID_ID == index_table_id || index_type >= INDEX_TYPE_MAX) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",
      K(ret), K(data_table_schema), K(index_table_id), K(INDEX_TYPE_MAX));
  } else if (OB_FAIL(index_table_ids.push_back(index_table_id))) {
  } else if (OB_FAIL(index_types.push_back(index_type))) {
  } else if (OB_FAIL(update_indexes_type(data_table_schema,
                                        index_table_ids, index_types,
                                        ddl_stmt_str,
                                        trans))) {
  }

  return ret;
}

int ObDDLOperator::update_indexes_type(const ObTableSchema &data_table_schema,
                                      const ObIArray<uint64_t> &index_table_ids,
                                      const ObIArray<ObIndexType> &index_types,
                                      const common::ObString *ddl_stmt_str,
                                      common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();

  uint64_t data_table_id = data_table_schema.get_table_id();

  if (OB_INVALID_ID == data_table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(data_table_id));
  } else if (index_table_ids.count() != index_types.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(index_table_ids), K(index_types), K(data_table_schema));
  } else if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("schema service should not be NULL", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < index_table_ids.count(); i++) {
      uint64_t index_table_id = index_table_ids.at(i);
      ObIndexType index_type = index_types.at(i);
      if (OB_INVALID_ID == data_table_id || OB_INVALID_ID == index_table_id
          || index_type <= INDEX_TYPE_IS_NOT || index_type >= INDEX_TYPE_MAX) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(ret), K(data_table_id), K(index_table_id), K(index_type));
      } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_service->get_table_sql_service().update_index_type(
                                                      data_table_schema, index_table_id,
                                                      index_type, new_schema_version,
                                                      ddl_stmt_str,
                                                      trans))) {
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().update_data_table_schema_version(
                                                      trans,
                                                      data_table_id,
                                                      data_table_schema.get_in_offline_ddl_white_list()))) {
  }
  return ret;
}

int ObDDLOperator::update_table_attribute(ObTableSchema &new_table_schema,
                                          common::ObMySQLTransaction &trans,
                                          const ObSchemaOperationType operation_type,
                                          const ObString *ddl_stmt_str/*=NULL*/)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  const bool update_object_status_ignore_version = false;
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    RS_LOG(WARN, "schema_service_impl must not null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    new_table_schema.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service_impl->get_table_sql_service().update_table_attribute(
        trans,
        new_table_schema,
        operation_type,
        update_object_status_ignore_version,
        ddl_stmt_str))) {
    }
  }
  return ret;
}

int ObDDLOperator::update_single_column(common::ObMySQLTransaction &trans,
                                        const ObTableSchema &origin_table_schema,
                                        const ObTableSchema &new_table_schema,
                                        ObColumnSchemaV2 &column_schema,
                                        const bool need_del_stats)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    RS_LOG(WARN, "schema_service_impl must not null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    column_schema.set_schema_version(new_schema_version);
    const ObColumnSchemaV2 *orig_column_schema = origin_table_schema.get_column_schema(column_schema.get_column_id());
    if (OB_FAIL(schema_service_impl->get_table_sql_service().update_single_column(
              trans, origin_table_schema, new_table_schema, column_schema,
              true /* record_ddl_operation */, need_del_stats))) {
    }
  }
  return ret;
}

int ObDDLOperator::batch_update_system_table_columns(
    common::ObMySQLTransaction &trans,
    const share::schema::ObTableSchema &orig_table_schema,
    share::schema::ObTableSchema &new_table_schema,
    const common::ObIArray<uint64_t> &add_column_ids,
    const common::ObIArray<uint64_t> &alter_column_ids,
    const common::ObString *ddl_stmt_str/*=NULL*/)
{
  int ret = OB_SUCCESS;

  const uint64_t table_id = new_table_schema.get_table_id();
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  const bool need_del_stats = false;
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    LOG_WARN("schema_service_impl must not null", KR(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    (void) new_table_schema.set_schema_version(new_schema_version);
    ObColumnSchemaV2 *new_column = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < add_column_ids.count(); i++) {
      const uint64_t column_id = add_column_ids.at(i);
      if (OB_ISNULL(new_column = new_table_schema.get_column_schema(column_id))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get column", KR(ret), K(table_id), K(column_id));
      } else if (FALSE_IT(new_column->set_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_service_impl->get_table_sql_service().insert_single_column(
                 trans, new_table_schema, *new_column, false))) {
      }
    } // end for

    for (int64_t i = 0; OB_SUCC(ret) && i < alter_column_ids.count(); i++) {
      const uint64_t column_id = alter_column_ids.at(i);
      if (OB_ISNULL(new_column = new_table_schema.get_column_schema(column_id))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get column", KR(ret), K(table_id), K(column_id));
      } else if (FALSE_IT(new_column->set_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_service_impl->get_table_sql_service().update_single_column(
                 trans, orig_table_schema, new_table_schema, *new_column, false, need_del_stats))) {
      }
    } // end for

    if (FAILEDx(schema_service_impl->get_table_sql_service().update_table_options(trans,
            orig_table_schema, new_table_schema, OB_DDL_ALTER_TABLE, ddl_stmt_str))) {
      LOG_WARN("failed to update table options", KR(ret), K(table_id));
    }
  }
  return ret;
}

int ObDDLOperator::update_partition_option(common::ObMySQLTransaction &trans,
                                           ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    RS_LOG(WARN, "schema_service_impl must not null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    if (OB_FAIL(schema_service_impl->get_table_sql_service().update_partition_option(
        trans, table_schema, new_schema_version))) {
    }
  }
  return ret;
}

int ObDDLOperator::update_partition_option(common::ObMySQLTransaction &trans,
                                           ObTableSchema &table_schema,
                                           const ObString &ddl_stmt_str)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    RS_LOG(WARN, "schema_service_impl must not null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    table_schema.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service_impl->get_table_sql_service().update_partition_option(
        trans, table_schema, &ddl_stmt_str))) {
    }
  }
  return ret;
}

int ObDDLOperator::update_check_constraint_state(common::ObMySQLTransaction &trans,
                                                 const ObTableSchema &table_schema,
                                                 ObConstraint &cst)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    RS_LOG(WARN, "schema_service_impl must not null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    cst.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service_impl->get_table_sql_service().update_check_constraint_state(trans,
                table_schema, cst))) {
    }
  }
  return ret;
}

int ObDDLOperator::sync_aux_schema_version_for_history(common::ObMySQLTransaction &trans,
                                                      const ObTableSchema &index_schema)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    RS_LOG(WARN, "schema_service_impl must not null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    if (OB_FAIL(schema_service_impl->get_table_sql_service().sync_aux_schema_version_for_history(
                trans, index_schema, new_schema_version))) {
    }
  }
  return ret;
}

int ObDDLOperator::drop_obj_privs(const uint64_t obj_id,
    const uint64_t obj_type,
    ObMySQLTransaction &trans,
    ObMultiVersionSchemaService &schema_service,
    ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_sql_service = schema_service.get_schema_service();
  ObArray<const ObObjPriv *> obj_privs;

  CK (OB_NOT_NULL(schema_sql_service));
  OZ (schema_guard.get_obj_priv_with_obj_id(obj_id, obj_type, obj_privs, true));
  for (int64_t i = 0; OB_SUCC(ret) && i < obj_privs.count(); ++i) {
    const ObObjPriv *obj_priv = obj_privs.at(i);
    int64_t new_schema_version = OB_INVALID_VERSION;

    if (OB_ISNULL(obj_priv)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("obj_priv priv is NULL", K(ret), K(obj_priv));
    } else {
      OZ (schema_service.gen_new_schema_version(new_schema_version));
      OZ (schema_sql_service->get_priv_sql_service().delete_obj_priv(
                *obj_priv, new_schema_version, trans));
      // In order to prevent being deleted, but there is no time to refresh the schema.
      // for example, obj priv has deleted, but obj schema unrefresh
      if (ret == OB_SEARCH_NOT_FOUND) {
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}


int ObDDLOperator::drop_obj_privs(const uint64_t obj_id,
    const uint64_t obj_type,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;

  OZ (schema_service_.get_runtime_schema_guard(schema_guard));
  OZ (drop_obj_privs(obj_id, obj_type, trans, schema_service_, schema_guard));

  return ret;
}

int ObDDLOperator::drop_tablet_of_table(
    const ObTableSchema &table_schema,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  {

    int64_t new_schema_version = OB_INVALID_VERSION;
    ObSEArray<const ObTableSchema*, 1> schemas;
    if (table_schema.is_vir_table()
        || table_schema.is_view_table()
        || is_inner_table(table_schema.get_table_id())) {
      // skip
    } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else {
      ObTabletDrop tablet_drop(trans, new_schema_version);
      if (OB_FAIL(schemas.push_back(&table_schema))) {
      } else if (OB_FAIL(tablet_drop.init())) {
      } else if (OB_FAIL(tablet_drop.add_drop_tablets_of_table_arg(schemas))) {
      } else if (OB_FAIL(tablet_drop.execute())) {
      }
    }
  }
  return ret;
}

int ObDDLOperator::drop_table(
    const ObTableSchema &table_schema,
    ObMySQLTransaction &trans,
    const ObString *ddl_stmt_str/*=NULL*/,
    const bool is_truncate_table/*false*/,
    DropTableIdHashSet *drop_table_set/*=NULL*/,
    const bool is_drop_db/*false*/,
    const bool delete_priv,
    const bool is_force_drop_lonely_lob_aux_table /*false*/)
{
  int ret = OB_SUCCESS;
  bool tmp = false;
  if (OB_FAIL(ObDependencyDDLHelper::modify_dep_obj_status(trans, table_schema.get_table_id(),
                                                      *this, schema_service_))) {
  } else if (OB_FAIL(drop_table_for_not_dropped_schema(
              table_schema, trans, ddl_stmt_str, is_truncate_table,
              drop_table_set, is_drop_db, delete_priv, is_force_drop_lonely_lob_aux_table))) {
  } else if (table_schema.is_view_table()
            && OB_FAIL(ObDependencyInfo::delete_schema_object_dependency(
                      trans,
                      table_schema.get_table_id(),
                      table_schema.get_schema_version(),
                      ObObjectType::VIEW))) {
    LOG_WARN("failed to delete_schema_object_dependency", K(ret), K(1UL),
    K(table_schema.get_table_id()));
  }

  if (OB_FAIL(ret)) {
  } else if (table_schema.is_aux_table()
      && !is_inner_table(table_schema.get_table_id())) {
    ObSnapshotInfoManager snapshot_mgr;
    ObArray<ObTabletID> tablet_ids;
    SCN invalid_scn;
    if (OB_FAIL(snapshot_mgr.init(GCTX.self_addr()))) {
    } else if (OB_FAIL(table_schema.get_tablet_ids(tablet_ids))) {
    } else if (OB_FAIL(snapshot_mgr.batch_release_snapshot_in_trans(
            trans, SNAPSHOT_FOR_DDL, -1/*schema_version*/, invalid_scn/*snapshot_scn*/, tablet_ids))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else {
    if (OB_FAIL(drop_tablet_of_table(table_schema, trans))) {
    }
  }

  if (OB_SUCC(ret)) {
    const uint64_t table_id = table_schema.get_table_id();
    if ((table_schema.is_vec_delta_buffer_type() || table_schema.is_hybrid_vec_index_log_type()) &&
               OB_FAIL(ObVectorIndexUtil::remove_dbms_vector_jobs(trans, table_schema.get_table_id()))) {
      LOG_WARN("failed to remove dbms vector jobs", K(ret), K(table_schema.get_table_id()));
    }
  }

  return ret;
}

int ObDDLOperator::drop_table_for_not_dropped_schema(
    const ObTableSchema &table_schema,
    ObMySQLTransaction &trans,
    const ObString *ddl_stmt_str/*=NULL*/,
    const bool is_truncate_table/*false*/,
    DropTableIdHashSet *drop_table_set/*=NULL*/,
    const bool is_drop_db/*false*/,
    const bool delete_priv,
    const bool is_force_drop_lonely_lob_aux_table /*false*/)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();
  ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service_impl must not null", K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  }
  //delete all object privileges granted on the object
  uint64_t obj_type = static_cast<uint64_t>(ObObjectType::TABLE);
  uint64_t table_id = table_schema.get_table_id();
  if (OB_SUCC(ret) && !is_drop_db && delete_priv) {
    OZ (drop_obj_privs(table_id, obj_type, trans), table_id, obj_type);
  } else {
    LOG_WARN("do not cascade drop obj priv", K(ret), K(is_drop_db), K(delete_priv));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(cleanup_autoinc_cache(table_schema))) {
  } else if (OB_FAIL(schema_service_impl->get_table_sql_service().drop_table(
                     table_schema,
                     new_schema_version,
                     trans,
                     ddl_stmt_str,
                     is_truncate_table,
                     is_drop_db,
                     is_force_drop_lonely_lob_aux_table,
                     &schema_guard,
                     drop_table_set))) {
  } else if (OB_FAIL(sync_version_for_cascade_table(table_schema.get_depend_table_ids(), trans))) {
  } else if (OB_FAIL(sync_version_for_cascade_mock_fk_parent_table(table_schema.get_depend_mock_fk_parent_table_ids(), trans))) {
  }
  return ret;
}

// ref
// When tables with auto-increment columns are frequently created or deleted, if the auto-increment column cache is not cleared, the memory will grow slowly.
// so every time when you drop table, if you bring auto-increment columns, clean up the corresponding cache.
int ObDDLOperator::cleanup_autoinc_cache(const ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  ObAutoincrementService &autoinc_service = share::ObAutoincrementService::get_instance();

  if (0 != table_schema.get_autoinc_column_id()) {
    uint64_t table_id = table_schema.get_table_id();
    uint64_t autoinc_column_id = table_schema.get_autoinc_column_id();
    LOG_INFO("begin to clear local auto-increment cache",
             K(table_id), K(autoinc_column_id));
    if (OB_FAIL(autoinc_service.clear_autoinc_cache(table_id,
                                                    autoinc_column_id))) {
    }
  }
  return ret;
}

int ObDDLOperator::drop_table_to_recyclebin(const ObTableSchema &table_schema,
                                            ObSchemaGetterGuard &schema_guard,
                                            ObMySQLTransaction &trans,
                                            const ObString *ddl_stmt_str,/*= NULL*/
                                            const bool is_truncate_table)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = schema_service_.get_schema_service();

  int64_t new_schema_version = OB_INVALID_VERSION;
  bool recycle_db_exist = false;
  if (OB_ISNULL(schema_service_impl)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("schema_service_impl must not null", K(ret));
  } else if (OB_FAIL(schema_guard.check_database_exist(OB_RECYCLEBIN_SCHEMA_ID,
                                                       recycle_db_exist))) {
  } else if (!recycle_db_exist) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("__recyclebin db not exist", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(cleanup_autoinc_cache(table_schema))) {
  } else if (OB_FAIL(ObDependencyDDLHelper::modify_dep_obj_status(trans, table_schema.get_table_id(),
                                                             *this, schema_service_))) {
  } else if (table_schema.is_view_table()
            && OB_FAIL(ObDependencyInfo::delete_schema_object_dependency(
                      trans,
                      table_schema.get_table_id(),
                      table_schema.get_schema_version(),
                      ObObjectType::VIEW))) {
    LOG_WARN("failed to delete_schema_object_dependency", K(ret), K(1UL),
    K(table_schema.get_table_id()));
  } else {
    ObTableSchema new_table_schema;
    if (OB_FAIL(new_table_schema.assign(table_schema))) {
    } else {
      ObSqlString new_table_name;
      //move to the recyclebin db
      new_table_schema.set_database_id(OB_RECYCLEBIN_SCHEMA_ID);
      new_table_schema.set_schema_version(new_schema_version);
      ObSchemaOperationType op_type = OB_INVALID_DDL_OP;
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(construct_new_name_for_recyclebin(new_table_schema, new_table_name))) {
      } else if (OB_FAIL(new_table_schema.set_table_name(new_table_name.string()))) {
      } else {
        ObRecycleObject recycle_object;
        recycle_object.set_object_name(new_table_name.string());
        recycle_object.set_original_name(table_schema.get_table_name_str());

        recycle_object.set_database_id(table_schema.get_database_id());
        recycle_object.set_table_id(table_schema.get_table_id());
        op_type = table_schema.is_view_table()
            ? OB_DDL_DROP_VIEW_TO_RECYCLEBIN : OB_DDL_DROP_TABLE_TO_RECYCLEBIN;
        if (is_truncate_table) {
          op_type = OB_DDL_TRUNCATE_DROP_TABLE_TO_RECYCLEBIN;
        }
        if (OB_FAIL(recycle_object.set_type_by_table_schema(table_schema))) {
        } else if (OB_FAIL(schema_service_impl->insert_recyclebin_object(recycle_object,
                                                                         trans))) {
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_service_impl->get_table_sql_service().update_table_options(
                    trans,
                    table_schema,
                    new_table_schema,
                    op_type,
                    ddl_stmt_str))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::restore_table_from_recyclebin(const ObTableSchema &table_schema,
                                                   ObTableSchema &new_table_schema,
                                                   ObMySQLTransaction &trans,
                                                   const uint64_t new_db_id,
                                                   const ObString &new_table_name,
                                                   const ObString *ddl_stmt_str,
                                                   ObSchemaGetterGuard &guard)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObArray<ObRecycleObject> recycle_objs;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObRecycleObject::RecycleObjType recycle_type = ObRecycleObject::get_type_by_table_schema(table_schema);
  ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service should not be null", K(ret));
  } else if (OB_FAIL(schema_service->fetch_recycle_object(table_schema.get_table_name_str(),
      recycle_type,
      trans,
      recycle_objs))) {
  } else if (recycle_objs.size() != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected recycle object num", K(ret),
             "table_name", table_schema.get_table_name_str(),
             "size", recycle_objs.size());
  } else {
    const ObRecycleObject &recycle_obj = recycle_objs.at(0);
    if (OB_FAIL(new_table_schema.assign(table_schema))) {
    } else if (new_db_id != OB_INVALID_ID) { // restore to new db
      new_table_schema.set_database_id(new_db_id);
      if (new_table_schema.is_aux_table()) {
        // should set the old name
        // When recovering a table to a new db, distinguish empty index name from renamed indexes.
        if (!new_table_name.empty() && OB_FAIL(new_table_schema.set_table_name(new_table_name))) {
          LOG_WARN("set new table name failed", K(ret));
        } else if (new_table_name.empty() && OB_FAIL(new_table_schema.set_table_name(recycle_obj.get_original_name()))) {
          LOG_WARN("set new table name failed", K(ret));
        } else if (new_table_schema.is_index_table()) {
          const int VISIBLE = 0;
          const uint64_t DROPINDEX = 0;
          new_table_schema.set_drop_index(DROPINDEX);
          if (!table_schema.is_invisible_before()) {
            new_table_schema.set_index_visibility(VISIBLE);
          }
          new_table_schema.set_invisible_before(0);
        }
      } else {
        if (!new_table_name.empty()) {
          if (OB_FAIL(new_table_schema.set_table_name(new_table_name))) {
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("database is valid, but no table name for data table",
                   K(ret), K(new_table_name));
        }
      }
    } else {
      //set original db_id
      const ObDatabaseSchema *db_schema = NULL;
      if (OB_FAIL(guard.get_database_schema(
                                            recycle_obj.get_database_id(),
                                            db_schema))) {
      } else if (NULL == db_schema) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("database not exist", K(recycle_obj), K(ret));
      } else if (db_schema->is_in_recyclebin()) {
        ret = OB_OP_NOT_ALLOW;
        LOG_WARN("restore table to __recyclebin database is not allowed",
                 K(recycle_obj), K(*db_schema), K(ret));
      } else if (OB_FAIL(new_table_schema.set_table_name(recycle_obj.get_original_name()))) {
      } else {
        new_table_schema.set_database_id(recycle_obj.get_database_id());
      }

      if (OB_SUCC(ret) && new_table_schema.is_index_table()) {
        const int VISIBLE = 0;
        const uint64_t DROPINDEX = 0;
        new_table_schema.set_drop_index(DROPINDEX);
        if (!table_schema.is_invisible_before()) {
          new_table_schema.set_index_visibility(VISIBLE);
        }
        new_table_schema.set_invisible_before(0);
      }
    }
    if (OB_SUCC(ret)) {
      bool is_table_exist = true;
      const int64_t table_schema_version = OB_INVALID_VERSION; // Take the latest local schema_guard
      ObSchemaOperationType op_type = new_table_schema.is_view_table()
          ? OB_DDL_RESTORE_VIEW_FROM_RECYCLEBIN : OB_DDL_RESTORE_TABLE_FROM_RECYCLEBIN;
      if (new_table_schema.is_index_table()) {
        op_type = OB_DDL_RECOVER_INDEX_FROM_RECYCLEBIN;
      }
      if (OB_FAIL(schema_service_.check_table_exist(new_table_schema.get_database_id(),
                                                           new_table_schema.get_table_name_str(),
                                                           new_table_schema.is_index_table(),
                                                           table_schema_version,
                                                           is_table_exist))) {
      } else if (is_table_exist) {
        ret = OB_ERR_TABLE_EXIST;
        LOG_USER_ERROR(OB_ERR_TABLE_EXIST, recycle_obj.get_original_name().length(),
                       recycle_obj.get_original_name().ptr());
      } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (FALSE_IT(new_table_schema.set_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_service->get_table_sql_service().update_table_options(
          trans,
          table_schema,
          new_table_schema,
          op_type,
          ddl_stmt_str))) {
      } else if (OB_FAIL(schema_service->delete_recycle_object(recycle_obj,
                                                               trans))) {
      }
    }
  }
  return ret;
}

int ObDDLOperator::purge_table_with_aux_table(
    const ObTableSchema &table_schema,
    ObSchemaGetterGuard &schema_guard,
    ObMySQLTransaction &trans,
    const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  if (!table_schema.is_aux_table()) {
    if (OB_FAIL(purge_aux_table(table_schema, schema_guard, trans, USER_INDEX))) {
    } else if (OB_FAIL(purge_aux_table(table_schema, schema_guard, trans,
                                       AUX_LOB_META))) {
    } else if (OB_FAIL(purge_aux_table(table_schema, schema_guard, trans,
                                       AUX_LOB_PIECE))) {
    } else if (OB_FAIL(ObPLDDLOperator::purge_table_trigger(table_schema, schema_guard, trans, *this))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(purge_table_in_recyclebin(table_schema,
                                          trans,
                                          ddl_stmt_str))) {
    }
  }
  return ret;
}

int ObDDLOperator::purge_aux_table(
    const ObTableSchema &table_schema,
    ObSchemaGetterGuard &schema_guard,
    ObMySQLTransaction &trans,
    const ObTableType table_type)
{
  int ret = OB_SUCCESS;

  ObSEArray<uint64_t, 16> aux_tid_array;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
  bool is_index = false;
  if (USER_INDEX == table_type) {
    is_index = true;
    if (OB_FAIL(table_schema.get_simple_index_infos(simple_index_infos))) {
    }
  } else if (AUX_LOB_META == table_type) {
    const uint64_t aux_lob_meta_tid = table_schema.get_aux_lob_meta_tid();
    if (OB_INVALID_ID != aux_lob_meta_tid && OB_FAIL(aux_tid_array.push_back(aux_lob_meta_tid))) {
      LOG_WARN("push back aux_lob_meta_tid failed", K(ret));
    }
  } else if (AUX_LOB_PIECE == table_type) {
    const uint64_t aux_lob_piece_tid = table_schema.get_aux_lob_piece_tid();
    if (OB_INVALID_ID != aux_lob_piece_tid && OB_FAIL(aux_tid_array.push_back(aux_lob_piece_tid))) {
      LOG_WARN("push back aux_lob_piece_tid failed", K(ret));
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid table type", K(ret), K(table_type));
  }

  int64_t N = is_index ? simple_index_infos.count() : aux_tid_array.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
    const ObTableSchema *aux_table_schema = NULL;
    uint64_t tid = is_index ? simple_index_infos.at(i).table_id_ : aux_tid_array.at(i);
    if (OB_FAIL(schema_guard.get_table_schema( tid, aux_table_schema))) {
    } else if (OB_ISNULL(aux_table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table schema should not be null", K(ret));
    } else if (OB_FAIL(purge_table_in_recyclebin(*aux_table_schema,
                                                 trans,
                                                 NULL /*ddl_stmt_str*/))) {
    }
  }

  return ret;
}

int ObDDLOperator::purge_table_in_recyclebin(const ObTableSchema &table_schema,
                                             ObMySQLTransaction &trans,
                                             const ObString *ddl_stmt_str/*=NULL*/)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObArray<ObRecycleObject> recycle_objs;
  ObRecycleObject::RecycleObjType recycle_type = ObRecycleObject::get_type_by_table_schema(table_schema);

  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service should not be null", K(ret));
  } else if (OB_FAIL(schema_service->fetch_recycle_object(table_schema.get_table_name_str(),
             recycle_type,
             trans,
             recycle_objs))) {
  } else if (recycle_objs.size() != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected recycle object num", K(ret), K(recycle_objs.size()));
  } else if (OB_FAIL(schema_service->delete_recycle_object(recycle_objs.at(0),
             trans))) {
  } else if (OB_FAIL(drop_table(table_schema, trans, ddl_stmt_str, false))) {
  }
  return ret;
}

int ObDDLOperator::create_index_in_recyclebin(ObTableSchema &table_schema,
                                              ObSchemaGetterGuard &schema_guard,
                                              ObMySQLTransaction &trans,
                                              const ObString *ddl_stmt_str) {
  int ret = OB_SUCCESS;

  if (table_schema.get_table_type() != USER_INDEX) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table_schema type is not index", K(ret));
  } else {
    ObSchemaService *schema_service_impl = schema_service_.get_schema_service();

    int64_t new_schema_version = OB_INVALID_VERSION;
    bool recycle_db_exist = false;
    if (OB_ISNULL(schema_service_impl)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("schema_service_impl must not be null", K(ret));
    } else if (OB_FAIL(schema_guard.check_database_exist(OB_RECYCLEBIN_SCHEMA_ID, recycle_db_exist))) {
    } else if (!recycle_db_exist) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("__recyclebin db not exist", K(ret));
    } else {
      ObSqlString new_table_name;
      ObTableSchema new_table_schema;
      if (OB_FAIL(new_table_schema.assign(table_schema))) {
      } else {
        new_table_schema.set_database_id(OB_RECYCLEBIN_SCHEMA_ID);
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(construct_new_name_for_recyclebin(table_schema, new_table_name))) {
      } else if (OB_FAIL(new_table_schema.set_table_name(new_table_name.string()))) {
      } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else {
        new_table_schema.set_schema_version(new_schema_version);
        ObRecycleObject recycle_object;
        recycle_object.set_object_name(new_table_name.string());
        recycle_object.set_original_name(table_schema.get_table_name_str());

        recycle_object.set_database_id(table_schema.get_database_id());
        recycle_object.set_table_id(table_schema.get_table_id());
        if (OB_FAIL(recycle_object.set_type_by_table_schema(table_schema))) {
        } else if (OB_FAIL(schema_service_impl->insert_recyclebin_object(recycle_object,
                trans))) {
        } else if (OB_FAIL(schema_service_impl->get_table_sql_service().create_table(
                new_table_schema,
                trans,
                ddl_stmt_str,
                true,
                true))) {
        } else if (OB_FAIL(sync_version_for_cascade_table(new_table_schema.get_depend_table_ids(), trans))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::restore_database_from_recyclebin(const ObDatabaseSchema &database_schema,
                                                      ObMySQLTransaction &trans,
                                                      const ObString &new_db_name,
                                                      ObSchemaGetterGuard &schema_guard,
                                                      const ObString &ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObArray<ObRecycleObject> recycle_objs;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service should not be null", K(ret));
  } else if (OB_FAIL(schema_service->fetch_recycle_object(database_schema.get_database_name(),
      ObRecycleObject::DATABASE,
      trans,
      recycle_objs))) {
  } else if (recycle_objs.size() != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected recycle object num", K(ret));
  } else {
    const ObRecycleObject &recycle_obj = recycle_objs.at(0);
    if (OB_SUCC(ret)) {
      ObDatabaseSchema new_db_schema = database_schema;
      new_db_schema.set_in_recyclebin(false);
      if (!new_db_name.empty()) {
        if (OB_FAIL(new_db_schema.set_database_name(new_db_name))) {
        }
      } else {
        //set original db_id
        if (OB_FAIL(new_db_schema.set_database_name(recycle_obj.get_original_name()))) {
        }
      }
      if (OB_SUCC(ret)) {
        bool is_database_exist = true;

        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_FAIL(schema_guard.check_database_exist(new_db_schema.get_database_name_str(),
                                                      is_database_exist))) {
        } else if (is_database_exist) {
          ret = OB_DATABASE_EXIST;
          LOG_USER_ERROR(OB_DATABASE_EXIST, new_db_schema.get_database_name_str().length(),
                         new_db_schema.get_database_name_str().ptr());
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (FALSE_IT(new_db_schema.set_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_service->get_database_sql_service().update_database(
            new_db_schema,
            trans,
            OB_DDL_RESTORE_DATABASE_FROM_RECYCLEBIN,
            &ddl_stmt_str))) {
        } else if (OB_FAIL(schema_service->delete_recycle_object(recycle_obj,
            trans))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::purge_table_of_database(const ObDatabaseSchema &db_schema,
                                           ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;

  const uint64_t database_id = db_schema.get_database_id();
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service should not be null", K(ret));
  } else {
    ObArray<ObRecycleObject> recycle_objs;
    if (OB_FAIL(schema_service->fetch_recycle_objects_of_db(database_id,
                                                            trans,
                                                            recycle_objs))) {
    } else {
      for (int i = 0; OB_SUCC(ret) && i < recycle_objs.count(); ++i) {
        const ObRecycleObject &recycle_obj = recycle_objs.at(i);
        const ObTableSchema* table_schema = NULL;
        if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
        } else if (OB_FAIL(schema_guard.get_table_schema(
                                                         recycle_obj.get_table_id(),
                                                         table_schema))) {
        } else if (OB_ISNULL(table_schema)) {
          ret = OB_TABLE_NOT_EXIST;
          LOG_WARN("table is not exist", K(ret), K(recycle_obj));
          ObCStringHelper helper;
          LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(db_schema.get_database_name_str()),
                         helper.convert(recycle_obj.get_object_name()));
        } else if (OB_FAIL(purge_table_with_aux_table(*table_schema,
                                                      schema_guard,
                                                      trans,
                                                      NULL /*ddl_stmt_str */))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::purge_database_in_recyclebin(const ObDatabaseSchema &database_schema,
                                                ObMySQLTransaction &trans,
                                                const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service should not be null", K(ret));
  } else {
    ObArray<ObRecycleObject> recycle_objs;
    if (OB_FAIL(schema_service->fetch_recycle_object(database_schema.get_database_name_str(),
       ObRecycleObject::DATABASE,
       trans,
       recycle_objs))) {
     } else if (1 != recycle_objs.size()) {
       ret = OB_ERR_UNEXPECTED;
       LOG_WARN("unexpected recycle object num", K(ret));
     } else if (OB_FAIL(drop_database(database_schema,
                                      trans,
                                      ddl_stmt_str))) {
     } else if (OB_FAIL(schema_service->delete_recycle_object(recycle_objs.at(0),
         trans))) {
     }
  }
  return ret;
}

int ObDDLOperator::fetch_expire_recycle_objects(const int64_t expire_time,
    ObIArray<ObRecycleObject> &recycle_objs)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service should not be null", K(ret));
  } else if (OB_FAIL(schema_service->fetch_expire_recycle_objects(expire_time,
                                                          sql_proxy_,
                                                          recycle_objs))) {
  }
  return ret;
}

int ObDDLOperator::init_runtime_schemas(
    const ObServerRuntimeSchema &runtime_schema,
    const ObSysVariableSchema &sys_variable,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;


  if (OB_FAIL(init_runtime_databases(runtime_schema, trans))) {
  } else if (OB_FAIL(init_runtime_optimizer_stats_info(sys_variable, trans))) {
  } else if (OB_FAIL(init_runtime_users(trans))) {
  } else if (OB_FAIL(init_freeze_info(trans))) {
  } else if (OB_FAIL(init_srs(trans))) {
  }

  return ret;
}

int ObDDLOperator::init_runtime_database(const ObServerRuntimeSchema &runtime_schema,
                                        const ObString &db_name,
                                        const uint64_t pure_db_id,
                                        const ObString &db_comment,
                                        ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  int64_t start = ObTimeUtility::current_time();

  int64_t new_schema_version = OB_INVALID_VERSION;
  if (db_name.empty() || OB_INVALID_ID == pure_db_id || db_comment.empty()) {
    ret = OB_INVALID_ARGUMENT;
    RS_LOG(WARN, "invalid argument", K(db_name), K(pure_db_id), K(db_comment), K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    ObSchemaService *schema_service = schema_service_.get_schema_service();
    ObDatabaseSchema db_schema;

    db_schema.set_database_id(pure_db_id);
    db_schema.set_database_name(db_name);
    db_schema.set_comment(db_comment);
    db_schema.set_schema_version(new_schema_version);
    if (db_name == OB_RECYCLEBIN_SCHEMA_NAME
        || db_name == OB_PUBLIC_SCHEMA_NAME) {
      db_schema.set_read_only(true);
    }

    if (OB_ISNULL(schema_service)) {
      ret = OB_ERR_SYS;
      RS_LOG(ERROR, "schema_service must not null");
    } else if (OB_FAIL(ObSchema::set_charset_and_collation_options(runtime_schema.get_charset_type(),
                                                                   runtime_schema.get_collation_type(),
                                                                   db_schema))) {
    } else if (OB_FAIL(schema_service->get_database_sql_service().insert_database(db_schema, trans))) {
    }
  }

  // init database priv
  if (OB_SUCC(ret)) {

    ObOriginalDBKey db_key;
    db_key.user_id_ = OB_SYS_USER_ID;
    db_key.db_ = db_name;

    ObSchemaService *schema_service = schema_service_.get_schema_service();
    if (OB_ISNULL(schema_service)) {
      ret = OB_ERR_SYS;
      RS_LOG(ERROR, "schema_service must not null");
    } else {
      ObSqlString ddl_stmt_str;
      ObString ddl_sql;
      ObNeedPriv need_priv;
      need_priv.db_ = db_name;
      need_priv.priv_set_ = OB_PRIV_DB_ACC;//is collect?
      need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
      if (OB_FAIL(ObDDLSqlGenerator::gen_db_priv_sql(ObAccountArg(OB_SYS_USER_NAME,
                                                     OB_SYS_HOST_NAME),
                                                     need_priv,
                                                     true, /*is_grant*/
                                                     ddl_stmt_str))) {
      } else if (FALSE_IT(ddl_sql = ddl_stmt_str.string())) {
      } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_service->get_priv_sql_service().grant_database(
          db_key, OB_PRIV_DB_ACC, new_schema_version, &ddl_sql, trans))) {
      }
    }
  }
  LOG_INFO("init runtime database", K(ret),
           "database_name", db_name,
           "cost", ObTimeUtility::current_time() - start);
  return ret;
}

int ObDDLOperator::init_runtime_databases(const ObServerRuntimeSchema &runtime_schema,
                                         ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  ObString oceanbase_schema(OB_SYS_DATABASE_NAME);
  ObString mysql_schema(OB_MYSQL_SCHEMA_NAME);
  ObString information_schema(OB_INFORMATION_SCHEMA_NAME);
  ObString recyclebin_schema(OB_RECYCLEBIN_SCHEMA_NAME);
  ObString public_schema(OB_PUBLIC_SCHEMA_NAME);
  ObString test_schema(OB_TEST_SCHEMA_NAME);
  if (OB_FAIL(init_runtime_database(runtime_schema, oceanbase_schema,
                                   OB_SYS_DATABASE_ID, "system database",
                                   trans))) {
  } else if (OB_FAIL(init_runtime_database(runtime_schema, recyclebin_schema,
                                          OB_RECYCLEBIN_SCHEMA_ID, "recyclebin schema",
                                          trans))) {
  } else if (OB_FAIL(init_runtime_database(runtime_schema, public_schema,
                                          OB_PUBLIC_SCHEMA_ID, "public schema",
                                          trans))) {
  } else {
    if (OB_FAIL(init_runtime_database(runtime_schema, mysql_schema,
                                     OB_MYSQL_SCHEMA_ID, "MySql schema",
                                     trans))) {
    } else if (OB_FAIL(init_runtime_database(runtime_schema, information_schema,
                                            OB_INFORMATION_SCHEMA_ID, "information_schema",
                                            trans))) {
    } else if (OB_FAIL(init_runtime_database(runtime_schema, test_schema,
                                            OB_INITIAL_TEST_DATABASE_ID, "test schema",
                                            trans))) {
    }
  }

  return ret;
}

int ObDDLOperator::init_runtime_optimizer_stats_info(const ObSysVariableSchema &sys_variable,
                                                    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSqlString prefs_sql;

  int64_t expected_affected_rows1 = 0;
  int64_t affected_rows1 = 0;
  if (OB_FAIL(ObDbmsStatsPreferences::gen_init_global_prefs_sql(prefs_sql,
                                                                false,
                                                                &expected_affected_rows1))) {
  } else if (OB_FAIL(ObDbmsStatsMaintenanceWindow::get_stats_maintenance_window_jobs_sql(
                                                                        sys_variable,
                                                                        trans))) {
  } else if (OB_UNLIKELY(prefs_sql.empty())) {
    ret = OB_ERR_UNEXPECTED;
    RS_LOG(WARN, "get unexpected empty", K(ret), K(prefs_sql));
  } else if (OB_FAIL(trans.write(prefs_sql.ptr(), affected_rows1))) {
  } else if (OB_UNLIKELY(affected_rows1 != expected_affected_rows1)) {
    ret = OB_ERR_UNEXPECTED;
    RS_LOG(WARN, "get unexpected affected_rows", K(ret), K(affected_rows1), K(expected_affected_rows1));
  } else {/*do nothing*/}
  return ret;
}

/*
 * The following system permissions are not granted to dba and need to be extracted from the complete set of permissions
-----------------------------------------
EXEMPT ACCESS POLICY
EXEMPT IDENTITY POLICY
EXEMPT REDACTION POLICY
INHERIT ANY PRIVILEGES
KEEP DATE TIME
KEEP SYSGUID
PURGE DBA_RECYCLEBIN
SYSDBA
SYSOPER
TRANSLATE ANY SQL
UNLIMITED TABLESPACE
------------------------------------------
resource role, pre define sys priv;
RESOURCE CREATE TABLE                             NO  YES YES
RESOURCE CREATE OPERATOR                          NO  YES YES
RESOURCE CREATE TRIGGER                           NO  YES YES
RESOURCE CREATE INDEXTYPE                         NO  YES YES
RESOURCE CREATE PROCEDURE                         NO  YES YES*/

int ObDDLOperator::init_inner_user_privs(ObUserInfo &user,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  int64_t new_schema_version = OB_INVALID_VERSION;
  uint64_t grantee_id = user.get_user_id();
  uint64_t option = NO_OPTION;
  ObRawPrivArray raw_priv_array;
  ObSchemaService *schema_service = schema_service_.get_schema_service();

  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service must not null", K(ret));
  }
  return ret;
}

int ObDDLOperator::init_runtime_user(const ObString &user_name,
                                    const ObString &pwd_raw,
                                    const uint64_t pure_user_id,
                                    const ObString &user_comment,
                                    ObMySQLTransaction &trans,
                                    const bool set_locked,
                                    const bool is_user)
{
  int ret = OB_SUCCESS;
  ObString pwd_enc;
  char enc_buf[ENC_BUF_LEN] = {0};
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObUserInfo user;

  pwd_enc.assign_ptr(enc_buf, ENC_BUF_LEN);
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service must not null");
  } else if (pwd_raw.length() > 0
             && OB_FAIL(ObEncryptedHelper::encrypt_passwd_to_stage2(pwd_raw, pwd_enc))) {
    LOG_WARN("Encrypt password failed", K(ret), K(pwd_raw));
  } else if (OB_FAIL(user.set_user_name(user_name))) {
  } else if (OB_FAIL(user.set_host(OB_SYS_HOST_NAME))) {
  } else if (OB_FAIL(user.set_passwd(pwd_enc))) {
  } else if (OB_FAIL(user.set_info(user_comment))) {
  } else {
    user.set_is_locked(set_locked);
    user.set_user_id(pure_user_id);
    if (is_user) {
      user.set_priv_set(OB_PRIV_ALL | OB_PRIV_GRANT);
    }
    user.set_schema_version(OB_CORE_SCHEMA_VERSION);
    user.set_type((is_user) ? OB_USER : OB_ROLE);
  }
  if (OB_SUCC(ret)) {
    ObSqlString ddl_stmt_str;
    ObString ddl_sql;
    if (OB_FAIL(ObDDLSqlGenerator::gen_create_user_sql(ObAccountArg(user.get_user_name_str(),
                                                       user.get_host_name_str(),
                                                       user.is_role()),
                                                       user.get_passwd_str(),
                                                       ddl_stmt_str))) {
    } else if (FALSE_IT(ddl_sql = ddl_stmt_str.string())) {
    } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else if (OB_FAIL(schema_service->get_user_sql_service().create_user(
                       user, new_schema_version, &ddl_sql, trans))) {
    }
  }
  return ret;
}

int ObDDLOperator::init_runtime_users(ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  ObString sys_user_name(OB_SYS_USER_NAME);
  if (OB_FAIL(init_runtime_user(sys_user_name, ObString(""), OB_SYS_USER_ID,
      "system administrator", trans))) {
  }
  return ret;
}

int ObDDLOperator::init_freeze_info(ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  int64_t start = ObTimeUtility::current_time();
  ObFreezeInfoProxy freeze_info_proxy{};
  ObFreezeInfo frozen_status;
  frozen_status.set_initial_value(DATA_CURRENT_VERSION);
  // init freeze_info in __all_freeze_info
  if (OB_FAIL(freeze_info_proxy.set_freeze_info(trans, frozen_status))) {
  }

  LOG_INFO("init freeze info", K(ret),
           "cost", ObTimeUtility::current_time() - start);
  return ret;
}

int ObDDLOperator::init_srs(ObMySQLTransaction &trans)
{
  // todo : import srs_id 0 in srs mgr init
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t start = ObTimeUtility::current_time();
  int64_t expected_rows = 1;
  if (OB_FAIL(sql.assign_fmt("INSERT INTO %s "
      "(SRS_VERSION, SRS_ID, SRS_NAME, ORGANIZATION, ORGANIZATION_COORDSYS_ID, DEFINITION, minX, maxX, minY, maxY, proj4text, DESCRIPTION) VALUES"
      R"((1, 0, '', NULL, NULL, '', -2147483648,2147483647,-2147483648,2147483647,'', NULL))",
      OB_ALL_SPATIAL_REFERENCE_SYSTEMS_TNAME))) {
  }

  if (OB_SUCC(ret)) {
    int64_t affected_rows = 0;
    if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
    } else if (expected_rows != affected_rows) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected affected_rows", K(expected_rows), K(affected_rows));
    }
  }

  LOG_INFO("init spatial reference systems", K(ret),
           "cost", ObTimeUtility::current_time() - start);
  return ret;
}

//----Functions for managing privileges----
int ObDDLOperator::create_user(
    const share::schema::ObUserInfo &user_info,
    const ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_sql_service must not be null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_sql_service->get_user_sql_service().create_user(
                     user_info, new_schema_version, ddl_stmt_str, trans))) {
  }
  return ret;
}

int ObDDLOperator::drop_user(
    const uint64_t user_id,
    const common::ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user_id must not be null", K(user_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama sql service and schema manager must not be null",
              K(schema_sql_service), K(ret));
  }
  //delete user
  if (OB_SUCC(ret)) {
    int64_t new_schema_version = OB_INVALID_VERSION;
    ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
    } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_sql_service->get_user_sql_service().drop_user(user_id, new_schema_version, ddl_stmt_str, trans, schema_guard))) {
    }
  }
  //delete db and table privileges of this user
  if (OB_SUCC(ret)) {
    if (OB_FAIL(drop_db_table_privs(user_id, trans))) {
    }
  }

  return ret;
}

int ObDDLOperator::drop_db_table_privs(
    const uint64_t user_id,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  int64_t ddl_count = 0;
  if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user_id must not be null", K(user_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama sql service and schema manager must not be null",
              K(schema_sql_service), K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  }
  // delete database privileges of this user
  if (OB_SUCC(ret)) {
    ObArray<const ObDBPriv *> db_privs;
    if (OB_FAIL(schema_guard.get_db_priv_with_user_id(user_id, db_privs))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < db_privs.count(); ++i) {
        const ObDBPriv *db_priv = db_privs.at(i);
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_ISNULL(db_priv)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("db priv is NULL", K(ret), K(db_priv));
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().delete_db_priv(
            db_priv->get_original_key(), new_schema_version, trans, schema_guard))) {
        }
      }
      ddl_count -= db_privs.count();
    }
  }
  // delete table privileges of this user MYSQL
  if (OB_SUCC(ret)) {
    ObArray<const ObTablePriv *> table_privs;
    if (OB_FAIL(schema_guard.get_table_priv_with_user_id(user_id, table_privs))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < table_privs.count(); ++i) {
        const ObTablePriv *table_priv = table_privs.at(i);
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_ISNULL(table_priv)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table priv is NULL", K(ret), K(table_priv));
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().delete_table_priv(
            table_priv->get_sort_key(), new_schema_version, trans, schema_guard))) {
        }
      }
    }
  }

  // delete column privileges of this user MYSQL
  if (OB_SUCC(ret)) {
    ObArray<const ObColumnPriv *> column_privs;
    if (OB_FAIL(schema_guard.get_column_priv_with_user_id(user_id, column_privs))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < column_privs.count(); ++i) {
        const ObColumnPriv *column_priv = column_privs.at(i);
        int64_t new_schema_version = OB_INVALID_VERSION;
        ObPrivSet empty_priv = 0;
        ObString dcl_stmt;
        if (OB_ISNULL(column_priv)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table priv is NULL", K(ret), K(column_priv));
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().grant_column(
            column_priv->get_sort_key(), column_priv->get_priv_id(), empty_priv,
            new_schema_version, &dcl_stmt, trans, false))) {
        }
      }
    }
  }

  // delete object privileges of this user
  if (OB_SUCC(ret)) {
    ObArray<const ObObjPriv *> obj_privs;

    OZ (schema_guard.get_obj_priv_with_grantee_id(user_id, obj_privs));
    OZ (schema_guard.get_obj_priv_with_grantor_id(user_id, obj_privs, false));
    for (int64_t i = 0; OB_SUCC(ret) && i < obj_privs.count(); ++i) {
      const ObObjPriv *obj_priv = obj_privs.at(i);
      int64_t new_schema_version = OB_INVALID_VERSION;
      if (OB_ISNULL(obj_priv)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("obj_priv priv is NULL", K(ret), K(obj_priv));
      } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().delete_obj_priv(
                 *obj_priv, new_schema_version, trans))) {
      }
    }
  }

  // delete routine privileges of this user MYSQL
  if (OB_SUCC(ret)) {
    ObArray<const ObRoutinePriv *> routine_privs;
    if (OB_FAIL(schema_guard.get_routine_priv_with_user_id(user_id, routine_privs))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < routine_privs.count(); ++i) {
        const ObRoutinePriv *routine_priv = routine_privs.at(i);
        int64_t new_schema_version = OB_INVALID_VERSION;
        ObPrivSet empty_priv = 0;
        ObString dcl_stmt;
        if (OB_ISNULL(routine_priv)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table priv is NULL", K(ret), K(routine_priv));
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().grant_routine(
            routine_priv->get_sort_key(), empty_priv, new_schema_version, &dcl_stmt, trans,
            0, false, "", ""))) {
        }
      }
    }
  }

  // delete object privileges of this user MYSQL
  if (OB_SUCC(ret)) {
    ObArray<const ObObjMysqlPriv *> obj_mysql_privs;
    if (OB_FAIL(schema_guard.get_obj_mysql_priv_with_user_id( user_id, obj_mysql_privs))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < obj_mysql_privs.count(); ++i) {
        const ObObjMysqlPriv *obj_mysql_priv = obj_mysql_privs.at(i);
        int64_t new_schema_version = OB_INVALID_VERSION;
        ObPrivSet empty_priv = 0;
        ObString dcl_stmt;
        if (OB_ISNULL(obj_mysql_priv)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("obj mysql priv is NULL", K(ret), K(obj_mysql_priv));
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().grant_object(
            obj_mysql_priv->get_sort_key(), empty_priv, new_schema_version, &dcl_stmt, trans,
            0, false, "", ""))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::rename_user(
    const uint64_t user_id,
    const ObAccountArg &new_account,
    const common::ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user_id must not be null", K(user_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    const ObUserInfo *user_info = NULL;
    int64_t new_schema_version = OB_INVALID_VERSION;
    if (OB_FAIL(schema_guard.get_user_info(user_id, user_info))) {
    } else if (OB_ISNULL(user_info)) {
      ret = OB_ERR_USER_NOT_EXIST;
      LOG_WARN("User not exist", K(ret));
    } else {
      ObUserInfo new_user_info = *user_info;
      new_user_info.set_user_name(new_account.user_name_);
      new_user_info.set_host(new_account.host_name_);
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_sql_service->get_user_sql_service().rename_user(
                  new_user_info, new_schema_version, ddl_stmt_str, trans))) {
      }
    }
  }
  return ret;
}

int ObDDLOperator::set_passwd(
    const uint64_t user_id,
    const common::ObString &passwd,
    const ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user_id must not be null", K(user_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    const ObUserInfo *user_info = NULL;
    if (OB_FAIL(schema_guard.get_user_info(user_id, user_info))) {
    } else if (OB_ISNULL(user_info)) {
      ret = OB_ERR_USER_NOT_EXIST;
      LOG_WARN("User not exist", K(ret));
    } else {
      int64_t new_schema_version = OB_INVALID_VERSION;
      ObUserInfo new_user_info = *user_info;
      new_user_info.set_passwd(passwd);
      new_user_info.set_password_last_changed(ObTimeUtility::current_time());
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_sql_service->get_user_sql_service().set_passwd(
                        new_user_info, new_schema_version, ddl_stmt_str, trans))) {
      }
    }
  }

  return ret;
}

int ObDDLOperator::set_max_connections(
    const uint64_t user_id,
    const uint64_t max_connections_per_hour,
    const uint64_t max_user_connections,
    const ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user_id must not be null", K(user_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    const ObUserInfo *user_info = NULL;
    if (OB_FAIL(schema_guard.get_user_info(user_id, user_info))) {
    } else if (OB_ISNULL(user_info)) {
      ret = OB_ERR_USER_NOT_EXIST;
      LOG_WARN("User not exist", K(ret));
    } else {
      int64_t new_schema_version = OB_INVALID_VERSION;
      ObUserInfo new_user_info = *user_info;
      if (OB_INVALID_ID != max_connections_per_hour) {
        new_user_info.set_max_connections(max_connections_per_hour);
      }
      if (OB_INVALID_ID != max_user_connections) {
        new_user_info.set_max_user_connections(max_user_connections);
      }
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_sql_service->get_user_sql_service().set_max_connections(
                        new_user_info, new_schema_version, ddl_stmt_str, trans))) {
      }
    }
  }

  return ret;
}

int ObDDLOperator::alter_role(
    const uint64_t role_id,
    const common::ObString &passwd,
    const ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_INVALID_ID == role_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("role_id must not be null", K(role_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    const ObUserInfo *role_info = NULL;
    if (OB_FAIL(schema_guard.get_user_info(role_id, role_info))) {
    } else if (OB_ISNULL(role_info)) {
      ret = OB_ROLE_NOT_EXIST;
      LOG_WARN("Role not exist", K(ret));
    } else {
      int64_t new_schema_version = OB_INVALID_VERSION;
      ObUserInfo new_role_info = *role_info;
      new_role_info.set_passwd(passwd);
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_sql_service->get_user_sql_service().alter_role(
                         new_role_info, new_schema_version, ddl_stmt_str, trans))) {
      }
    }
  }

  return ret;
}

int ObDDLOperator::alter_user_default_role(const ObString &ddl_str,
                                           const ObUserInfo &schema,
                                           ObIArray<uint64_t> &role_id_array,
                                           ObIArray<uint64_t> &disable_flag_array,
                                           ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_sql_service = NULL;
  int64_t new_schema_version = OB_INVALID_VERSION;

  if (OB_ISNULL(schema_sql_service = schema_service_.get_schema_service())) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_sql_service must not null", K(ret));
  } else if (!schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    if (OB_FAIL(schema_sql_service->get_priv_sql_service().alter_user_default_role(
                                                          schema,
                                                          new_schema_version,
                                                          &ddl_str,
                                                          role_id_array,
                                                          disable_flag_array,
                                                          trans))) {
    }
  }

  return ret;
}

int ObDDLOperator::alter_user_require(const uint64_t user_id,
    const obcall::ObSetPasswdArg &arg,
    const common::ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user_id must not be null", K(user_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    const ObUserInfo *user_info = NULL;
    if (OB_FAIL(schema_guard.get_user_info(user_id, user_info))) {
    } else if (OB_ISNULL(user_info)) {
      ret = OB_ERR_USER_NOT_EXIST;
      LOG_WARN("User not exist", K(ret));
    } else {
      int64_t new_schema_version = OB_INVALID_VERSION;
      ObUserInfo new_user_info = *user_info;
      new_user_info.set_ssl_type(arg.ssl_type_);
      new_user_info.set_ssl_cipher(arg.ssl_cipher_);
      new_user_info.set_x509_issuer(arg.x509_issuer_);
      new_user_info.set_x509_subject(arg.x509_subject_);
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_sql_service->get_user_sql_service().alter_user_require(
                         new_user_info, new_schema_version, ddl_stmt_str, trans))) {
      }
    }
  }

  return ret;
}

int ObDDLOperator::grant_revoke_user(
    const uint64_t user_id,
    const ObPrivSet priv_set,
    const bool grant,
    const bool is_from_inner_sql,
    const ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user_id must not be null", K(user_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    ObPrivSet new_priv = priv_set;

    const ObUserInfo *user_info = NULL;
    if (OB_FAIL(schema_guard.get_user_info(user_id, user_info)) ||
        NULL == user_info) {
      ret = OB_ERR_USER_NOT_EXIST;
      LOG_WARN("User not exist", K(ret));
    } else {
      if (grant) {
        new_priv = priv_set | user_info->get_priv_set();
      } else {
        new_priv = (~priv_set) & user_info->get_priv_set();
      }
      //no matter privilege change or not, write a sql
      int64_t new_schema_version = OB_INVALID_VERSION;
      ObUserInfo new_user_info = *user_info;
      new_user_info.set_priv_set(new_priv);
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_sql_service->get_user_sql_service().grant_revoke_user(
                         new_user_info, new_schema_version, ddl_stmt_str, trans, is_from_inner_sql))) {
      }
    }
  }

  return ret;
}

int ObDDLOperator::lock_user(
    const uint64_t user_id,
    const bool locked,
    const ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  ObSchemaGetterGuard schema_guard;
  if (OB_INVALID_ID == user_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("user_id is invalid", K(user_id), K(ret));
  } else if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    const ObUserInfo *user_info = NULL;
    if (OB_FAIL(schema_guard.get_user_info(user_id, user_info)) ||
          NULL == user_info) {
      ret = OB_ERR_USER_NOT_EXIST;
      LOG_WARN("User not exist", K(ret));
    } else if (locked != user_info->get_is_locked()) {
      int64_t new_schema_version = OB_INVALID_VERSION;
      ObUserInfo new_user_info = *user_info;
      new_user_info.set_is_locked(locked);
      if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
      } else if (OB_FAIL(schema_sql_service->get_user_sql_service().lock_user(
                         new_user_info, new_schema_version, ddl_stmt_str, trans))) {
      }
    }
  }
  return ret;
}

int ObDDLOperator::grant_database(
    const ObOriginalDBKey &db_priv_key,
    const ObPrivSet priv_set,
    const ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (!db_priv_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("db_priv_key is invalid", K(db_priv_key), K(ret));
  } else if (0 == priv_set) {
    //do nothing
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    ObPrivSet new_priv = priv_set;
    bool need_flush = true;
    ObPrivSet db_priv_set = OB_PRIV_SET_EMPTY;
    if (OB_FAIL(schema_guard.get_db_priv_set(db_priv_key, db_priv_set, true))) {
    } else {
      new_priv |= db_priv_set;
      need_flush = (new_priv != db_priv_set);
      if (need_flush) {
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().grant_database(db_priv_key,
                                                                              new_priv,
                                                                              new_schema_version,
                                                                              ddl_stmt_str,
                                                                              trans))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::revoke_database(
    const ObOriginalDBKey &db_priv_key,
    const ObPrivSet priv_set,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (!db_priv_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("db_priv_key is invalid", K(db_priv_key), K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    ObPrivSet db_priv_set = OB_PRIV_SET_EMPTY;
    if (OB_FAIL(schema_guard.get_db_priv_set(db_priv_key, db_priv_set, true))) {
    } else if (OB_PRIV_SET_EMPTY == db_priv_set) {
      ret = OB_ERR_NO_GRANT;
      LOG_WARN("No such grant to revoke", K(db_priv_key), K(ret));
    } else if (0 == priv_set) {
      //do nothing
    } else {
      ObPrivSet new_priv = db_priv_set & (~priv_set);
      if (db_priv_set & priv_set) {
        ObSqlString ddl_stmt_str;
        ObString ddl_sql;
        const ObUserInfo *user_info = NULL;
        ObNeedPriv need_priv;
        need_priv.db_ = db_priv_key.db_;
        need_priv.priv_level_ = OB_PRIV_DB_LEVEL;
        need_priv.priv_set_ = db_priv_set & priv_set; //priv to revoke
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_FAIL(schema_guard.get_user_info(db_priv_key.user_id_, user_info))) {
        } else if (OB_ISNULL(user_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("user not exist", K(db_priv_key), K(ret));
        } else if (OB_FAIL(ObDDLSqlGenerator::gen_db_priv_sql(ObAccountArg(user_info->get_user_name_str(), user_info->get_host_name_str()),
                                                              need_priv,
                                                              false, /*is_grant*/
                                                              ddl_stmt_str))) {
        } else if (FALSE_IT(ddl_sql = ddl_stmt_str.string())) {
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().revoke_database(
            db_priv_key, new_priv, new_schema_version, &ddl_sql, trans))) {
        }
      }
    }
  }
  return ret;
}

/* According to the current obj priv, check if the user has all the permissions listed in obj_priv_array */
int ObDDLOperator::check_obj_privs_exists(
    ObSchemaGetterGuard &schema_guard,
    const share::schema::ObObjPrivSortKey &obj_priv_key, /* in: obj priv key */
    const ObRawObjPrivArray &obj_priv_array,      /* in: privs to be deleted */
    ObRawObjPrivArray &option_priv_array,         /* out: privs to be deleted cascade */
    bool &is_all)                                 /* out: obj priv array is all privs existed */
{
  int ret = OB_SUCCESS;
  ObPackedObjPriv obj_privs = 0;
  ObRawObjPriv raw_obj_priv = 0;
  bool exists = false;
  uint64_t option_out = false;
  is_all = false;
  int org_n = 0;
  OZ (schema_guard.get_obj_privs(obj_priv_key, obj_privs));
  for (int i = 0; i < obj_priv_array.count() && OB_SUCC(ret); i++) {
    raw_obj_priv = obj_priv_array.at(i);
    OZ (ObOraPrivCheck::raw_obj_priv_exists_with_info(raw_obj_priv,
                                                      obj_privs,
                                                      exists,
                                                      option_out),
        raw_obj_priv, obj_privs, ret);
    if (OB_SUCC(ret)) {
      if (!exists) {
      ret = OB_ERR_CANNOT_REVOKE_PRIVILEGES_YOU_DID_NOT_GRANT;
      } else if (option_out == GRANT_OPTION) {
        OZ (option_priv_array.push_back(raw_obj_priv));
      }
    }
  }
  OZ (ObPrivPacker::get_total_obj_privs(obj_privs, org_n));
  OX (is_all = org_n == obj_priv_array.count());
  return ret;
}

/** According to the current obj priv, check if the user has all the permissions listed in obj_priv_array, including the column permissions on the table.
 * check_obj_privs_exists_including_col_privs
 * is used to check the existence of object permissions
 * @param  {ObSchemaGetterGuard &} schema_guard                 : schema_guard
 * @param  {const schema::ObObjPrivSortKey &} obj_priv_key      : object for checking, accurate to the table
 * @param  {const ObRawObjPrivArray &} obj_priv_array           : permission for revoking
 * @param  {ObIArray<schema::ObObjPrivSortKey> &} new_key_array : There may be multiple columns on the obj_priv_key
 *         table object with independent column permissions, so we regenerate all the keys,
 *         which are accurate to the column
 * @param  {ObIArray<ObPackedObjPriv> &} new_packed_privs_array : Corresponds to new_key_array,
 *         permission to revoke
 * @param  {ObIArray<bool> &} is_all                            : Corresponds to new_key_array,
 *         indicating whether the permission to revoke is all the permissions owned by the key
 * @return {int}                                                : ret
 */
int ObDDLOperator::check_obj_privs_exists_including_col_privs(
    ObSchemaGetterGuard &schema_guard,
    const share::schema::ObObjPrivSortKey &obj_priv_key,
    const ObRawObjPrivArray &obj_priv_array,
    ObIArray<share::schema::ObObjPrivSortKey> &new_key_array,
    ObIArray<ObPackedObjPriv> &new_packed_privs_array,
    ObIArray<bool> &is_all)
{
  int ret = OB_SUCCESS;
  ObRawObjPriv raw_obj_priv_to_be_revoked = 0;
  ObPackedObjPriv packed_table_privs = 0;
  ObPackedObjPriv packed_table_privs_to_be_revoked = 0;
  ObSEArray<uint64_t, 4> col_id_array;
  ObSEArray<ObPackedObjPriv, 4> packed_col_privs_array;
  ObPackedObjPriv packed_total_matched_privs = 0;
  ObObjPrivSortKey new_col_key = obj_priv_key;
  ObPackedObjPriv packed_col_privs = 0;
  ObPackedObjPriv packed_col_privs_to_be_revoked = 0;
  bool exists = false;
  int org_n = 0;
  int own_priv_count = 0;
  int revoked_priv_count = 0;
  uint64_t option_out = false;
  bool is_all_single = false;
  new_key_array.reset();
  new_packed_privs_array.reset();
  is_all.reset();
  // 1. Find all object permissions based on grantee_id, grantor_id, obj_type, obj_id.
  OZ (build_table_and_col_priv_array_for_revoke_all(schema_guard,
                                                    obj_priv_key,
                                                    packed_table_privs,
                                                    col_id_array,
                                                    packed_col_privs_array));
  CK (col_id_array.count() == packed_col_privs_array.count());
  // 2. check permissions of table level.
  for (int i = 0; OB_SUCC(ret) && i < obj_priv_array.count(); ++i) {
    raw_obj_priv_to_be_revoked = obj_priv_array.at(i);
    // Check whether the table-level permission exists on the table
    OZ (ObOraPrivCheck::raw_obj_priv_exists_with_info(raw_obj_priv_to_be_revoked,
                                                      packed_table_privs,
                                                      exists,
                                                      option_out),
        raw_obj_priv_to_be_revoked, packed_table_privs, ret);
    if (OB_SUCC(ret)) {
      // If it exists, add the permission together with option to packed_table_privs_to_be_revoked,
      // and added in packed_total_matched_privs means that the permission was found
      // The permission may not exist as a table-level permission on the table
      if (exists) {
        OZ (ObPrivPacker::append_raw_obj_priv(option_out,
                                              raw_obj_priv_to_be_revoked,
                                              packed_table_privs_to_be_revoked));
        OZ (ObPrivPacker::append_raw_obj_priv(option_out,
                                              raw_obj_priv_to_be_revoked,
                                              packed_total_matched_privs));
      }
    }
  }
  // Record the table key and its permission to be revoke in the return value
  if (packed_table_privs_to_be_revoked) {
    OZ (new_key_array.push_back(obj_priv_key));
    OZ (new_packed_privs_array.push_back(packed_table_privs_to_be_revoked));
    OZ (ObPrivPacker::get_total_obj_privs(packed_table_privs, own_priv_count));
    OZ (ObPrivPacker::get_total_obj_privs(packed_table_privs_to_be_revoked, revoked_priv_count));
    OX (is_all_single = own_priv_count == revoked_priv_count);
    OZ (is_all.push_back(is_all_single));
  }
  // 3. Check column permissions
  for (int i = 0; OB_SUCC(ret) && i < col_id_array.count(); ++i) {
    // each column
    new_col_key.col_id_ = col_id_array.at(i);
    packed_col_privs = packed_col_privs_array.at(i);
    packed_col_privs_to_be_revoked = 0;
    for (int i = 0; OB_SUCC(ret) && i < obj_priv_array.count(); ++i) {
      raw_obj_priv_to_be_revoked = obj_priv_array.at(i);
      // Check only if the permission may be a column permission
      if (ObOraPrivCheck::raw_priv_can_be_granted_to_column(raw_obj_priv_to_be_revoked)) {
        // Check if the permission exists on the column
        OZ (ObOraPrivCheck::raw_obj_priv_exists_with_info(raw_obj_priv_to_be_revoked,
                                                          packed_col_privs,
                                                          exists,
                                                          option_out),
            raw_obj_priv_to_be_revoked, packed_col_privs, ret);
        if (OB_SUCC(ret)) {
          if (exists) {
            OZ (ObPrivPacker::append_raw_obj_priv(option_out,
                                                  raw_obj_priv_to_be_revoked,
                                                  packed_col_privs_to_be_revoked));
            OZ (ObPrivPacker::append_raw_obj_priv(option_out,
                                                  raw_obj_priv_to_be_revoked,
                                                  packed_total_matched_privs));
          }
        }
      }
    }
    // According to whether there are permissions in packed_col_privs_to_be_revoked, decide whether to keep the key
    if (OB_SUCC(ret)) {
      if (packed_col_privs_to_be_revoked) {
        OZ (new_key_array.push_back(new_col_key));
        OZ (new_packed_privs_array.push_back(packed_col_privs_to_be_revoked));
        OZ (ObPrivPacker::get_total_obj_privs(packed_col_privs, own_priv_count));
        OZ (ObPrivPacker::get_total_obj_privs(packed_col_privs_to_be_revoked, revoked_priv_count));
        OX (is_all_single = own_priv_count == revoked_priv_count);
        OZ (is_all.push_back(is_all_single));
      }
    }
  }
  // The three arrays should be the same size after processing.
  CK (new_key_array.count() == new_packed_privs_array.count());
  CK (new_key_array.count() == is_all.count());
  // According to the number of packed_total_matched_privs, determine whether to try to revoke a permission that does not exist
  OZ (ObPrivPacker::get_total_obj_privs(packed_total_matched_privs, org_n));
  if (OB_SUCC(ret)) {
    if (org_n < obj_priv_array.count()) {
      ret = OB_ERR_CANNOT_REVOKE_PRIVILEGES_YOU_DID_NOT_GRANT;
      LOG_WARN("try to revoke non exists privs", K(ret), K(org_n), K(obj_priv_array.count()));
    }
  }
  return ret;
}

/* According to the current obj priv, determine which ones need to be newly added priv array */
int ObDDLOperator::set_need_flush_ora(
    ObSchemaGetterGuard &schema_guard,
    const share::schema::ObObjPrivSortKey &obj_priv_key,   /* in: obj priv key*/
    const uint64_t option,                          /* in: new option */
    const ObRawObjPrivArray &obj_priv_array,        /* in: new privs used want to add */
    ObRawObjPrivArray &new_obj_priv_array)          /* out: new privs actually to be added */
{
  int ret = OB_SUCCESS;
  ObPackedObjPriv obj_privs = 0;
  ObRawObjPriv raw_obj_priv = 0;
  bool exists = false;
  OZ (schema_guard.get_obj_privs(obj_priv_key, obj_privs));
  for (int i = 0; i < obj_priv_array.count() && OB_SUCC(ret); i++) {
    raw_obj_priv = obj_priv_array.at(i);
    OZ (ObOraPrivCheck::raw_obj_priv_exists(raw_obj_priv,
                                            option,
                                            obj_privs,
                                            exists),
        raw_obj_priv, option, obj_privs, ret);
    if (OB_SUCC(ret) && !exists) {
      OZ (new_obj_priv_array.push_back(raw_obj_priv));
    }
  }
  return ret;
}

/* Only handle authorization for one object, for example, one table, one column */
int ObDDLOperator::grant_table(
    const ObTablePrivSortKey &table_priv_key,
    const ObPrivSet priv_set,
    const ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans,
    const share::ObRawObjPrivArray &obj_priv_array,
    const uint64_t option,
    const share::schema::ObObjPrivSortKey &obj_priv_key,
    const common::ObString &grantor,
    const common::ObString &grantor_host)
{
  int ret = OB_SUCCESS;
  ObRawObjPrivArray new_obj_priv_array;

  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (!table_priv_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table_priv_key is invalid", K(table_priv_key), K(ret));
  } else if (0 == priv_set && obj_priv_array.count() == 0) {
    //do nothing
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    ObPrivSet new_priv = priv_set;
    ObPrivSet table_priv_set = OB_PRIV_SET_EMPTY;
    if (OB_FAIL(schema_guard.get_table_priv_set(table_priv_key, table_priv_set))) {
    } else {
      bool need_flush = true;
      new_priv |= table_priv_set;
      need_flush = (new_priv != table_priv_set);
      if (need_flush) {
        int64_t new_schema_version = OB_INVALID_VERSION;
        int64_t new_schema_version_ora = OB_INVALID_VERSION;
        if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (obj_priv_array.count() > 0) {
          OZ (set_need_flush_ora(schema_guard, obj_priv_key, option, obj_priv_array,
            new_obj_priv_array));
          if (new_obj_priv_array.count() > 0) {
            OZ (schema_service_.gen_new_schema_version(new_schema_version_ora));
          }
        }
        OZ (schema_sql_service->get_priv_sql_service().grant_table(
            table_priv_key, new_priv, new_schema_version, ddl_stmt_str, trans,
            new_obj_priv_array, option, obj_priv_key, new_schema_version_ora, true, false,
            grantor, grantor_host), table_priv_key, ret, false);
      } else if (obj_priv_array.count() > 0) {
        OZ (set_need_flush_ora(schema_guard, obj_priv_key, option, obj_priv_array,
          new_obj_priv_array));
        if (new_obj_priv_array.count() > 0) {
          int64_t new_schema_version_ora = OB_INVALID_VERSION;
          OZ (schema_service_.gen_new_schema_version(new_schema_version_ora));
          OZ (schema_sql_service->get_priv_sql_service().grant_table_ora_only(
            ddl_stmt_str, trans, new_obj_priv_array, option, obj_priv_key,
            new_schema_version_ora, false, false),table_priv_key, ret);
        }
      }
    }
  }

  return ret;
}

int ObDDLOperator::grant_routine(
    const ObRoutinePrivSortKey &routine_priv_key,
    const ObPrivSet priv_set,
    common::ObMySQLTransaction &trans,
    const uint64_t option,
    const bool gen_ddl_stmt,
    const common::ObString &grantor,
    const common::ObString &grantor_host)
{
  int ret = OB_SUCCESS;
  ObRawObjPrivArray new_obj_priv_array;

  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (!routine_priv_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("routine_priv_key is invalid", K(routine_priv_key), K(ret));
  } else if (0 == priv_set) {
    //do nothing
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    ObPrivSet new_priv = priv_set;
    ObPrivSet routine_priv_set = OB_PRIV_SET_EMPTY;
    if (OB_FAIL(schema_guard.get_routine_priv_set(routine_priv_key, routine_priv_set))) {
    } else {
      bool need_flush = true;
      new_priv |= routine_priv_set;
      need_flush = (new_priv != routine_priv_set);
      if (need_flush) {
        ObSqlString ddl_stmt_str;
        ObString ddl_sql;
        const ObUserInfo *user_info = NULL;
        ObNeedPriv need_priv;
        need_priv.db_ = routine_priv_key.db_;
        need_priv.table_ = routine_priv_key.routine_;
        need_priv.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
        need_priv.priv_set_ = (~routine_priv_set) & new_priv;
        need_priv.obj_type_ = routine_priv_key.routine_type_ == ObRoutineType::ROUTINE_PROCEDURE_TYPE ?
                                                      ObObjectType::PROCEDURE : ObObjectType::FUNCTION;
        if (OB_FAIL(schema_guard.get_user_info(routine_priv_key.user_id_, user_info))) {
        } else if (OB_ISNULL(user_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("user not exist", K(routine_priv_key), K(ret));
        } else if (gen_ddl_stmt == true && OB_FAIL(ObDDLSqlGenerator::gen_routine_priv_sql(
            ObAccountArg(user_info->get_user_name_str(), user_info->get_host_name_str()),
            need_priv, true, /*is_grant*/ ddl_stmt_str))) {
          LOG_WARN("gen_routine_priv_sql failed", K(ret), K(need_priv));
        } else if (FALSE_IT(ddl_sql = ddl_stmt_str.string())) {
        } else {
          int64_t new_schema_version = OB_INVALID_VERSION;
          int64_t new_schema_version_ora = OB_INVALID_VERSION;
          if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
          } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().grant_routine(
                routine_priv_key, new_priv, new_schema_version, &ddl_sql, trans, option, true,
                grantor, grantor_host))) {
          }
        }
      }
    }
  }

  return ret;
}

int ObDDLOperator::grant_column(
    ObSchemaGetterGuard &schema_guard,
    const ObColumnPrivSortKey &column_priv_key,
    const ObPrivSet priv_set,
    const ObString *ddl_stmt_str,
    common::ObMySQLTransaction &trans,
    const bool is_grant)
{
  int ret = OB_SUCCESS;

  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service, K(ret));
  } else if (!column_priv_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("column_priv_key is invalid", K(column_priv_key), K(ret));
  } else if (0 == priv_set) {
    //do nothing
  } else {
    ObPrivSet new_priv = OB_PRIV_SET_EMPTY;
    ObPrivSet column_priv_set = OB_PRIV_SET_EMPTY;
    uint64_t column_priv_id = OB_INVALID_ID;
    if (OB_FAIL(schema_guard.get_column_priv_id(column_priv_key.user_id_, column_priv_key.db_,
                                                column_priv_key.table_, column_priv_key.column_, column_priv_id))) {
    } else if (column_priv_id == OB_INVALID_ID) {
      if (!is_grant) {
        ret = OB_ERR_CANNOT_REVOKE_PRIVILEGES_YOU_DID_NOT_GRANT;
        LOG_WARN("revoke no such grant", K(ret), K(column_priv_key));
      } else {
        uint64_t new_column_priv_id = OB_INVALID_ID;
        if (OB_FAIL(schema_sql_service->fetch_new_priv_id(new_column_priv_id))) {
        } else if (OB_UNLIKELY(OB_INVALID_ID == new_column_priv_id)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("object_id is invalid", KR(ret));
        } else {
          column_priv_id = new_column_priv_id;
        }
      }
    } else if (OB_FAIL(schema_guard.get_column_priv_set(column_priv_key, column_priv_set))) {
    }

    if (OB_SUCC(ret)) {
      bool need_flush = true;
      if (is_grant) {
        new_priv = column_priv_set | priv_set;
      } else {
        new_priv = column_priv_set & (~priv_set);
      }
      need_flush = (new_priv != column_priv_set);

      if (need_flush) {
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().grant_column(
                              column_priv_key, column_priv_id, new_priv, new_schema_version,
                              ddl_stmt_str, trans, is_grant))) {
        }
      }
    }
  }

  return ret;
}

/* in: grantor, grantee, obj_type, obj_id
   out: table_packed_privs
        array of col_id which has col privs
        array of col_packed_privs */
int ObDDLOperator::build_table_and_col_priv_array_for_revoke_all(
    ObSchemaGetterGuard &schema_guard,
    const ObObjPrivSortKey &obj_priv_key,
    ObPackedObjPriv &packed_table_priv,
    ObSEArray<uint64_t, 4> &col_id_array,
    ObSEArray<ObPackedObjPriv, 4> &packed_privs_array)
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObObjPriv *, 4> obj_priv_array;
  uint64_t col_id = 0;
  CK (obj_priv_key.is_valid());
  OZ (schema_guard.get_obj_privs_in_grantor_ur_obj_id(obj_priv_key,
                                                      obj_priv_array));
  for (int i = 0; i < obj_priv_array.count() && OB_SUCC(ret); i++) {
    const ObObjPriv *obj_priv = obj_priv_array.at(i);
    if (obj_priv != NULL) {
      col_id = obj_priv->get_col_id();
      if (col_id == OBJ_LEVEL_FOR_TAB_PRIV) {
        packed_table_priv = obj_priv->get_obj_privs();
      } else {
        OZ (col_id_array.push_back(col_id));
        OZ (packed_privs_array.push_back(obj_priv->get_obj_privs()));
      }
    }
  }

  return ret;
}

int ObDDLOperator::revoke_table_all(
    ObSchemaGetterGuard &schema_guard,
    const ObObjPrivSortKey &obj_priv_key,
    ObString &ddl_sql,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  int64_t new_schema_version = OB_INVALID_VERSION;
  share::ObPackedObjPriv packed_table_privs = 0;
  ObRawObjPrivArray raw_priv_array;
  ObRawObjPrivArray option_raw_array;
  ObSEArray<uint64_t, 4> col_id_array;
  ObSEArray<ObPackedObjPriv, 4> packed_privs_array;
  ObObjPrivSortKey new_key = obj_priv_key;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service,
        K(ret));
  }
  OZ (build_table_and_col_priv_array_for_revoke_all(schema_guard,
                                                    obj_priv_key,
                                                    packed_table_privs,
                                                    col_id_array,
                                                    packed_privs_array));
  if (OB_SUCC(ret)) {
    // 1. table-level permissions
    if (packed_table_privs > 0) {
      OZ (ObPrivPacker::raw_obj_priv_from_pack(packed_table_privs, raw_priv_array));
      OZ (schema_service_.gen_new_schema_version(new_schema_version));
      OZ (schema_sql_service->get_priv_sql_service().revoke_table_ora(
        new_key, raw_priv_array, new_schema_version, &ddl_sql, trans, true));
      OZ (ObPrivPacker::raw_option_obj_priv_from_pack(packed_table_privs, option_raw_array));
      OZ (revoke_obj_cascade(schema_guard, new_key.grantee_id_,
          trans, new_key, option_raw_array));
    }
    // 2. column-level permissions
    for (int i = 0; i < col_id_array.count() && OB_SUCC(ret); i++) {
      new_key.col_id_ = col_id_array.at(i);
      OZ (ObPrivPacker::raw_obj_priv_from_pack(packed_privs_array.at(i), raw_priv_array));
      OZ (schema_service_.gen_new_schema_version(new_schema_version));
      OZ (schema_sql_service->get_priv_sql_service().revoke_table_ora(
        new_key, raw_priv_array, new_schema_version, &ddl_sql, trans, true));
      OZ (ObPrivPacker::raw_option_obj_priv_from_pack(packed_privs_array.at(i),
          option_raw_array));
      OZ (revoke_obj_cascade(schema_guard, new_key.grantee_id_,
          trans, new_key, option_raw_array));
    }
  }
  return ret;
}

int ObDDLOperator::build_next_level_revoke_obj(
    ObSchemaGetterGuard &schema_guard,
    const ObObjPrivSortKey &old_key,
    ObObjPrivSortKey &new_key,
    ObIArray<const ObObjPriv *> &obj_privs)
{
  int ret = OB_SUCCESS;
  new_key = old_key;
  new_key.grantor_id_ = new_key.grantee_id_;
  OZ (schema_guard.get_obj_privs_in_grantor_obj_id(new_key,
                                                   obj_privs));
  return ret;
}

/* After processing the top-level revoke obj, then call this function to process revoke recursively.
   1. According to the obj key of the upper layer, change grantee to grantor and find new permissions that need to be reclaimed
   2. If there are permissions that need to be reclaimed, call revoke obj ora, if not, end.
   3. calling self. If a new grantee is found back to the original grantee, the end */
int ObDDLOperator::revoke_obj_cascade(
    ObSchemaGetterGuard &schema_guard,
    const uint64_t start_grantee_id,     /* in: check circle */
    common::ObMySQLTransaction &trans,
    const ObObjPrivSortKey &old_key,     /* in: old key */
    ObRawObjPrivArray &old_array)        /* in: privs that have grantable option */
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  ObObjPrivSortKey new_key;
  ObRawObjPrivArray grantable_array;
  ObRawObjPrivArray new_array;
  ObSEArray<const ObObjPriv *, 4> obj_privs;
  bool is_all = false;
  if (old_array.count() > 0) {
    OZ (build_next_level_revoke_obj(schema_guard, old_key, new_key, obj_privs));
    /* If there are multiple, it means that this user has delegated to multiple other users */
    if (obj_privs.count() > 0) {
      ObPackedObjPriv old_p_list;
      ObPackedObjPriv old_opt_p_list;
      ObPackedObjPriv privs_revoke_this_level;
      ObPackedObjPriv privs_revoke_next_level;

      OZ (ObPrivPacker::pack_raw_obj_priv_list(NO_OPTION, old_array, old_p_list));
      OZ (ObPrivPacker::pack_raw_obj_priv_list(GRANT_OPTION, old_array, old_opt_p_list));

      for (int i = 0; OB_SUCC(ret) && i < obj_privs.count(); i++) {
        const ObObjPriv* obj_priv = obj_privs.at(i);
        if (obj_priv != NULL) {
          /* 1. cross join grantee privs and grantor privs without option */
          privs_revoke_this_level = old_p_list & obj_priv->get_obj_privs();

          if (privs_revoke_this_level > 0) {
            /* 2. build new_Key */
            new_key.grantee_id_ = obj_priv->get_grantee_id();
            /* 2. build new array */
            OZ (ObPrivPacker::raw_obj_priv_from_pack(privs_revoke_this_level, new_array));
            OZ (schema_service_.gen_new_schema_version(new_schema_version));
            OX (is_all = (new_array.count() == old_array.count()));
            OZ (schema_sql_service->get_priv_sql_service().revoke_table_ora(
                  new_key, new_array, new_schema_version, NULL, trans, is_all));
            /* 3. new grantee is equ org grantee. end */
            if (OB_SUCC(ret)) {
              if (new_key.grantee_id_ == start_grantee_id) {
              } else {
                /* 3. decide privs to be revoked recursively */
                privs_revoke_next_level = old_opt_p_list & obj_priv->get_obj_privs();
                if (privs_revoke_next_level > 0) {
                  OZ (ObPrivPacker::raw_obj_priv_from_pack(privs_revoke_next_level, new_array));
                  OZ (revoke_obj_cascade(schema_guard, start_grantee_id, trans,
                      new_key, new_array));
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

/* Get all foreign keys of a user referencing the specified parent table */
int ObDDLOperator::build_fk_array_by_parent_table(
  ObSchemaGetterGuard &schema_guard,
  const ObString &grantee_name,
  const ObString &db_name,
  const ObString &tab_name,
  ObIArray<ObDropForeignKeyArg> &drop_fk_array,
  ObIArray<uint64_t> &ref_tab_id_array)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;

  OZ (schema_guard.get_table_schema( db_name, tab_name, false, table_schema));
  if (OB_SUCC(ret)) {
    if (NULL == table_schema) {
      ret = OB_TABLE_NOT_EXIST;
    } else {
      uint64_t db_id = OB_INVALID_ID;
      OZ (schema_guard.get_database_id(grantee_name, db_id));
      /* Traverse all child tables referencing the parent table, if the owner of the child table is grantee, add drop fk array */
      const ObIArray<ObForeignKeyInfo> &fk_array = table_schema->get_foreign_key_infos();
      for (int i = 0; OB_SUCC(ret) && i < fk_array.count(); i++) {
        const ObForeignKeyInfo &fk_info = fk_array.at(i);
        const ObSimpleTableSchemaV2 *ref_table = NULL;
        OZ (schema_guard.get_simple_table_schema( fk_info.child_table_id_, ref_table));
        if (OB_SUCC(ret)) {
          if (ref_table == NULL) {
            ret = OB_TABLE_NOT_EXIST;
          } else if (ref_table->get_database_id() ==  db_id) {
            ObDropForeignKeyArg fk_arg;
            fk_arg.foreign_key_name_ = fk_info.foreign_key_name_;
            OZ (drop_fk_array.push_back(fk_arg));
            OZ (ref_tab_id_array.push_back(ref_table->get_table_id()));
          }
        }
      }
    }
  }

  return ret;
}

int ObDDLOperator::drop_fk_cascade(
    ObSchemaGetterGuard &schema_guard,
    bool has_ref_priv,
    bool has_no_cascade,
    const ObString &grantee_name,
    const ObString &parent_db_name,
    const ObString &parent_tab_name,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (has_ref_priv) {
    ObSEArray<ObDropForeignKeyArg, 4> drop_fk_array;
    ObSEArray<uint64_t, 4> ref_tab_id_array;
    OZ (build_fk_array_by_parent_table(schema_guard,
                                       grantee_name,
                                       parent_db_name,
                                       parent_tab_name,
                                       drop_fk_array,
                                       ref_tab_id_array));
    if (OB_SUCC(ret)) {
      if (drop_fk_array.count() > 0) {
        if (has_no_cascade) {
          ret = OB_ERR_CASCADE_CONSTRAINTS_MUST_BE_SPECIFIED_TO_PERFORM_THIS_REVOKE;
        } else {
          for (int i = 0; OB_SUCC(ret) && i < drop_fk_array.count(); i++) {
            const ObTableSchema *ref_tab = NULL;
            const ObDropForeignKeyArg &drop_fk = drop_fk_array.at(i);

            OZ (schema_guard.get_table_schema(
                ref_tab_id_array.at(i), ref_tab));
            if (OB_SUCC(ret)) {
              if (ref_tab == NULL) {
                ret = OB_TABLE_NOT_EXIST;
              } else {
                const ObForeignKeyInfo *parent_table_mock_foreign_key_info = NULL;
                OZ (alter_table_drop_foreign_key(*ref_tab, drop_fk, trans, parent_table_mock_foreign_key_info, ref_tab->get_in_offline_ddl_white_list()));
                if (OB_SUCC(ret) && NULL != parent_table_mock_foreign_key_info) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("parent_table_mock_foreign_key_info is unexpected", K(ret));
                }
              }
            }
          }
        }
      }
    }
  }

  return ret;
}

int ObDDLOperator::revoke_table(
    const ObTablePrivSortKey &table_priv_key,
    const ObPrivSet priv_set,
    common::ObMySQLTransaction &trans,
    const ObObjPrivSortKey &obj_priv_key,
    const share::ObRawObjPrivArray &obj_priv_array,
    const bool revoke_all_ora,
    const common::ObString &grantor,
    const common::ObString &grantor_host)
{
  int ret = OB_SUCCESS;

  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service,
        K(ret));
  } else if (!table_priv_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("db_priv_key is invalid", K(table_priv_key), K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    ObPrivSet table_priv_set = OB_PRIV_SET_EMPTY;
    if (OB_FAIL(schema_guard.get_table_priv_set(table_priv_key, table_priv_set))) {
    } else if (OB_PRIV_SET_EMPTY == table_priv_set
               && !revoke_all_ora
               && obj_priv_array.count() == 0) {
      ObArray<const ObColumnPriv *> column_privs;
      if (OB_FAIL(schema_guard.get_column_priv_in_table(table_priv_key, column_privs))) {
      } else {
        if (column_privs.count() > 0) {
          //do nothing here, and will revoke column priv behind.
        } else {
          ret = OB_ERR_CANNOT_REVOKE_PRIVILEGES_YOU_DID_NOT_GRANT;
          LOG_WARN("No such grant to revoke", K(table_priv_key), K(ret));
        }
      }
    } else if (0 == priv_set && obj_priv_array.count() == 0) {
      // do-nothing
    } else {
      ObPrivSet new_priv = table_priv_set & (~priv_set);
      /* If there is an intersection between the existing permissions and the permissions that require revoke */
      if (0 != (table_priv_set & priv_set)) {
        ObSqlString ddl_stmt_str;
        ObString ddl_sql;
        const ObUserInfo *user_info = NULL;
        ObNeedPriv need_priv;
        share::ObRawObjPrivArray option_priv_array;

        need_priv.db_ = table_priv_key.db_;
        need_priv.table_ = table_priv_key.table_;
        need_priv.priv_level_ = OB_PRIV_TABLE_LEVEL;
        need_priv.priv_set_ = table_priv_set & priv_set; //priv to revoke
        int64_t new_schema_version = OB_INVALID_VERSION;
        int64_t new_schema_version_ora = OB_INVALID_VERSION;
        bool is_all = false;
        bool has_ref_priv = false;
        if (OB_FAIL(check_obj_privs_exists(schema_guard, obj_priv_key,
            obj_priv_array, option_priv_array, is_all))) {
        } else if (OB_FAIL(schema_guard.get_user_info(table_priv_key.user_id_, user_info))) {
        } else if (OB_ISNULL(user_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("user not exist", K(table_priv_key), K(ret));
        } else if (OB_FAIL(drop_fk_cascade(schema_guard,
                                           has_ref_priv,
                                           true, /* has no cascade */
                                           user_info->get_user_name_str(), /* grantee name */
                                           table_priv_key.db_,
                                           table_priv_key.table_,
                                           trans))) {
        } else if (OB_FAIL(ObDDLSqlGenerator::gen_table_priv_sql(
                ObAccountArg(user_info->get_user_name_str(), user_info->get_host_name_str()),
                need_priv,
                false, /*is_grant*/
                ddl_stmt_str))) {
        } else if (FALSE_IT(ddl_sql = ddl_stmt_str.string())) {
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version_ora))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().revoke_table(
            table_priv_key, new_priv, new_schema_version, &ddl_sql, trans,
            new_schema_version_ora, obj_priv_key, obj_priv_array, is_all,
            grantor, grantor_host))) {
        } else {
          OZ (revoke_obj_cascade(schema_guard, obj_priv_key.grantee_id_,
              trans, obj_priv_key, option_priv_array));
        }
        // In revoke all statement, if you have permission, it will come here, and the content of mysql will be processed first.
        if (OB_SUCC(ret) && revoke_all_ora) {
          OZ (revoke_table_all(schema_guard, obj_priv_key, ddl_sql, trans));
        }
      } else {
        // do nothing
      }
    }
  }
  return ret;
}

int ObDDLOperator::revoke_routine(
    const ObRoutinePrivSortKey &routine_priv_key,
    const ObPrivSet priv_set,
    common::ObMySQLTransaction &trans,
    bool report_error,
    const bool gen_ddl_stmt,
    const common::ObString &grantor,
    const common::ObString &grantor_host)
{
  int ret = OB_SUCCESS;

  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_sql_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_sql_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schama service_impl and schema manage must not null",
        "schema_service_impl", schema_sql_service,
        K(ret));
  } else if (!routine_priv_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("db_priv_key is invalid", K(routine_priv_key), K(ret));
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    ObPrivSet routine_priv_set = OB_PRIV_SET_EMPTY;
    if (OB_FAIL(schema_guard.get_routine_priv_set(routine_priv_key, routine_priv_set))) {
    } else if (OB_PRIV_SET_EMPTY == routine_priv_set) {
      if (report_error) {
        ret = OB_ERR_CANNOT_REVOKE_PRIVILEGES_YOU_DID_NOT_GRANT;
        LOG_WARN("No such grant to revoke", K(routine_priv_key), K(routine_priv_set), K(ret));
      }
    } else if (0 == priv_set) {
      // do-nothing
    } else {
      ObPrivSet new_priv = routine_priv_set & (~priv_set);
      /* If there is an intersection between the existing permissions and the permissions that require revoke */
      if ((routine_priv_set & priv_set) != 0) {
        ObSqlString ddl_stmt_str;
        ObString ddl_sql;
        const ObUserInfo *user_info = NULL;
        ObNeedPriv need_priv;
        share::ObRawObjPrivArray option_priv_array;

        need_priv.db_ = routine_priv_key.db_;
        need_priv.table_ = routine_priv_key.routine_;
        need_priv.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
        need_priv.priv_set_ = routine_priv_set & priv_set; //priv to revoke
        need_priv.obj_type_ = routine_priv_key.routine_type_ == ObRoutineType::ROUTINE_PROCEDURE_TYPE ?
                                                      ObObjectType::PROCEDURE : ObObjectType::FUNCTION;
        int64_t new_schema_version = OB_INVALID_VERSION;
        int64_t new_schema_version_ora = OB_INVALID_VERSION;
        bool is_all = false;
        bool has_ref_priv = false;
        if (OB_FAIL(schema_guard.get_user_info(routine_priv_key.user_id_, user_info))) {
        } else if (OB_ISNULL(user_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("user not exist", K(routine_priv_key), K(ret));
        } else if (gen_ddl_stmt == true && OB_FAIL(ObDDLSqlGenerator::gen_routine_priv_sql(
            ObAccountArg(user_info->get_user_name_str(), user_info->get_host_name_str()),
            need_priv,
            false, /*is_grant*/
            ddl_stmt_str))) {
          LOG_WARN("gen_routine_priv_sql failed", K(ret), K(need_priv));
        } else if (FALSE_IT(ddl_sql = ddl_stmt_str.string())) {
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(schema_sql_service->get_priv_sql_service().revoke_routine(
            routine_priv_key, new_priv, new_schema_version, &ddl_sql, trans, grantor, grantor_host))) {
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::get_flush_role_array(
  const uint64_t option,
  const common::ObIArray<uint64_t> &org_role_ids,
  bool &need_flush,
  bool is_grant,
  const ObUserInfo &user_info,
  common::ObIArray<uint64_t> &role_ids)
{
  int ret = OB_SUCCESS;
  need_flush = false;
  if (org_role_ids.count() > 0) {
    if (is_grant) {
      for (int64_t i = 0; OB_SUCC(ret) && i < org_role_ids.count(); ++i) {
        const uint64_t role_id = org_role_ids.at(i);
        if (!user_info.role_exists(role_id, option)) {
          need_flush = true;
          OZ (role_ids.push_back(role_id));
        }
      }
    } else {
      need_flush = true;
      OZ (role_ids.assign(org_role_ids));
    }
  }
  return ret;
}

int ObDDLOperator::grant_revoke_role(
    const ObUserInfo &user_info,
    const common::ObIArray<uint64_t> &org_role_ids,
    // When specified_role_info is not empty, use it as role_info instead of reading it in the schema.
    const ObUserInfo *specified_role_info,
    common::ObMySQLTransaction &trans,
    const bool log_operation,
    const bool is_grant,
    const uint64_t option)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObSchemaGetterGuard schema_guard;
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObString ddl_sql;

  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service must not null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
  } else {
    common::ObSEArray<uint64_t, 8> role_ids;
    bool need_flush = false;
    OZ (get_flush_role_array(option,
                             org_role_ids,
                             need_flush,
                             is_grant,
                             user_info,
                             role_ids));
    if (OB_SUCC(ret) && need_flush) {
      common::ObSqlString sql_string;
      if (OB_FAIL(sql_string.append_fmt(is_grant ? "GRANT ": "REVOKE "))) {
      } else if (OB_NOT_NULL(specified_role_info)) {
        // Use single specified role info
        if (OB_FAIL(sql_string.append_fmt("%s", specified_role_info->get_user_name()))) {
        }
      } else {
        // Use role info obtained from schema
        for (int64_t i = 0; OB_SUCC(ret) && i < role_ids.count(); ++i) {
          const uint64_t role_id = role_ids.at(i);
          const ObUserInfo *role_info = NULL;
          if (0 != i) {
            if (OB_FAIL(sql_string.append_fmt(","))) {
            }
          }
          if (FAILEDx(schema_guard.get_user_info(role_id, role_info))) {
            LOG_WARN("Failed to get role info", K(ret), K(role_id));
          } else if (NULL == role_info) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("role doesn't exist", K(ret), K(role_id));
          } else if (OB_FAIL(sql_string.append_fmt("`%s`@`%s`",
                                                   role_info->get_user_name(),
                                                   role_info->get_host_name()))) {
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(sql_string.append_fmt(is_grant ? " TO `%s`@`%s`": " FROM `%s`@`%s`",
                                          user_info.get_user_name(),
                                          user_info.get_host_name()))) {
        } else if (is_grant && option != NO_OPTION && OB_FAIL(sql_string.append_fmt(
                                                                          " WITH ADMIN OPTION"))) {
          LOG_WARN("append sql failed", K(ret));
        } else {
          ddl_sql = sql_string.string();
          LOG_WARN("wang sql ", K(ddl_sql), K(sql_string));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_service->get_priv_sql_service().grant_revoke_role(user_info,
            role_ids,
            specified_role_info,
            new_schema_version,
            log_operation ? &ddl_sql : NULL,
            trans,
            is_grant,
            schema_guard,
            option))) {
        }
      }
    }
  }

  return ret;
}

int ObDDLOperator::get_flush_priv_array(
    const uint64_t option,
    const share::ObRawPrivArray &priv_array,
    const ObSysPriv *sys_priv,
    share::ObRawPrivArray &new_priv_array,
    bool &need_flush,
    const bool is_grant,
    const ObUserInfo &user_info)
{
  int ret = OB_SUCCESS;
  int64_t raw_priv = 0;
  bool exists = false;

  need_flush = FALSE;
  if (is_grant) {
    if (sys_priv == NULL) {
      need_flush = true;
      OZ (new_priv_array.assign(priv_array));
    } else {
      ARRAY_FOREACH(priv_array, idx) {
        raw_priv = priv_array.at(idx);
        OZ (ObOraPrivCheck::raw_sys_priv_exists(option,
                                                raw_priv,
                                                sys_priv->get_priv_array(),
                                                exists));
        if (OB_SUCC(ret) && !exists) {
          need_flush = true;
          OZ (new_priv_array.push_back(raw_priv));
        }
      }
    }
  }
  else {
    need_flush = true;
    if (sys_priv == NULL) {
      ret = OB_ERR_SYSTEM_PRIVILEGES_NOT_GRANTED_TO;
      LOG_USER_ERROR(OB_ERR_SYSTEM_PRIVILEGES_NOT_GRANTED_TO,
                     user_info.get_user_name_str().length(),
                     user_info.get_user_name_str().ptr());
      LOG_WARN("revoke sys priv fail, sys priv not exists", K(priv_array), K(ret));
    } else {
      ARRAY_FOREACH(priv_array, idx) {
        raw_priv = priv_array.at(idx);
        OZ (ObOraPrivCheck::raw_sys_priv_exists(option,
                                                raw_priv,
                                                sys_priv->get_priv_array(),
                                                exists));
        if (OB_SUCC(ret) && !exists) {
          ret = OB_ERR_SYSTEM_PRIVILEGES_NOT_GRANTED_TO;
          LOG_USER_ERROR(OB_ERR_SYSTEM_PRIVILEGES_NOT_GRANTED_TO,
                     user_info.get_user_name_str().length(),
                     user_info.get_user_name_str().ptr());
          LOG_WARN("revoke sys priv fail, sys priv not exists", K(priv_array), K(ret));
        }
        OZ (new_priv_array.push_back(raw_priv));
      }
    }
  }
  return ret;
}

int ObDDLOperator::grant_sys_priv_to_ur(
    const uint64_t grantee_id,
    const ObSysPriv* sys_priv,
    const uint64_t option,
    const ObRawPrivArray priv_array,
    common::ObMySQLTransaction &trans,
    const bool is_grant,
    const common::ObString *ddl_stmt_str,
    ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObRawPrivArray new_priv_array;
  bool need_flush;
  const ObUserInfo *user_info = NULL;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service must not null", K(ret));
  }
  OZ (schema_service_.gen_new_schema_version(new_schema_version));
  OZ (schema_guard.get_user_info(grantee_id, user_info));
  OZ (get_flush_priv_array(option,
                           priv_array,
                           sys_priv,
                           new_priv_array,
                           need_flush,
                           is_grant,
                           *user_info));
  if (OB_SUCC(ret) && need_flush) {
    if (is_grant) {
      CK (new_priv_array.count() > 0);
      OZ (schema_service->get_priv_sql_service().grant_sys_priv_to_ur(grantee_id,
                                                                      option,
                                                                      new_priv_array,
                                                                      new_schema_version,
                                                                      ddl_stmt_str,
                                                                      trans,
                                                                      is_grant,
                                                                      false));
    } else {
      int n_cnt = 0;
      bool revoke_all_flag = false;
      /* revoke */
      CK(sys_priv != NULL);
      OZ (ObPrivPacker::get_total_privs(sys_priv->get_priv_array(), n_cnt));
      revoke_all_flag = (n_cnt == new_priv_array.count());
        /* revoke all */
      OZ (schema_service->get_priv_sql_service().grant_sys_priv_to_ur(grantee_id,
                                                                      option,
                                                                      new_priv_array,
                                                                      new_schema_version,
                                                                      ddl_stmt_str,
                                                                      trans,
                                                                      is_grant,
                                                                      revoke_all_flag),
           1UL, grantee_id, new_priv_array, is_grant, revoke_all_flag);
    }
  }
  return ret;
}

//----End of functions for managing privileges----

//----Functions for managing outlines----
int ObDDLOperator::create_outline(ObOutlineInfo &outline_info,
                                  ObMySQLTransaction &trans,
                                  const ObString *ddl_stmt_str/*=NULL*/)
{
  int ret = OB_SUCCESS;
  uint64_t new_outline_id = OB_INVALID_ID;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();

  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service must not null", K(ret));
  } // else if (!outline_info.is_valid()) {
  //   ret = OB_INVALID_ARGUMENT;
  //   LOG_ERROR("outline is invalid", K(outline_info), K(ret));
  // }
  else if (OB_FAIL(schema_service->fetch_new_outline_id(new_outline_id))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    outline_info.set_outline_id(new_outline_id);
    outline_info.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_outline_sql_service().insert_outline(
        outline_info, trans, ddl_stmt_str))) {
    }
  }
  return ret;
}

int ObDDLOperator::replace_outline(ObOutlineInfo &outline_info,
                                   ObMySQLTransaction &trans,
                                   const ObString *ddl_stmt_str/*=NULL*/)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    outline_info.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_outline_sql_service().replace_outline(
        outline_info, trans, ddl_stmt_str))) {
    } else {/*do nothing*/}
  }
  return ret;
}

int ObDDLOperator::alter_outline(ObOutlineInfo &outline_info,
                                 ObMySQLTransaction &trans,
                                 const ObString *ddl_stmt_str/*=NULL*/)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    outline_info.set_schema_version(new_schema_version);
    if (OB_FAIL(schema_service->get_outline_sql_service().alter_outline(
        outline_info, trans, ddl_stmt_str))) {
    } else {/*do nothing*/}
  }
  return ret;
}

int ObDDLOperator::drop_outline(const uint64_t database_id,
                                const uint64_t outline_id,
                                ObMySQLTransaction &trans,
                                const ObString *ddl_stmt_str/*=NULL*/)
{
  int ret = OB_SUCCESS;
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_UNLIKELY(OB_INVALID_ID == database_id
                  || OB_INVALID_ID == outline_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(database_id), K(outline_id), K(ret));
  } else if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_ERROR("schema_service must not null", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_outline_sql_service().delete_outline(
      database_id,
      outline_id,
      new_schema_version,
      trans,
      ddl_stmt_str))) {
  } else {/*do nothing*/}
  return ret;
}

//----End of functions for managing outlines----

int ObDDLOperator::insert_ori_schema_version(
    ObMySQLTransaction &trans,
    const uint64_t table_id,
    const int64_t &ori_schema_version)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_INVALID_VERSION == ori_schema_version) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid schema version" , K(ret), K(table_id), K(ori_schema_version));
  } else if (OB_FAIL(schema_service->get_table_sql_service().insert_ori_schema_version(
             trans, table_id, ori_schema_version))) {
  }
  return ret;
}

int ObDDLOperator::drop_inner_generated_index_column(ObMySQLTransaction &trans,
                                                    ObSchemaGetterGuard &schema_guard,
                                                    const ObTableSchema &index_schema,
                                                    ObTableSchema &new_data_table_schema)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *data_table = NULL;
  const ObColumnSchemaV2 *index_col = NULL;

  uint64_t data_table_id = index_schema.get_data_table_id();
  if (OB_FAIL(schema_guard.get_table_schema( data_table_id, data_table))) {
  } else if (OB_ISNULL(data_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("data table schema is unknown", K(data_table_id));
  } else if (!new_data_table_schema.is_valid()) {
    if (OB_FAIL(new_data_table_schema.assign(*data_table))) {
    }
  }
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(new_data_table_schema.get_simple_index_infos(simple_index_infos))) {
  } else {
    new_data_table_schema.set_in_offline_ddl_white_list(index_schema.get_in_offline_ddl_white_list());
  }
  for (ObTableSchema::const_column_iterator iter = index_schema.column_begin();
       OB_SUCC(ret) && iter != index_schema.column_end();
       ++iter) {
    ObColumnSchemaV2 *column_schema = (*iter);
    if (OB_ISNULL(column_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, column schema is nullptr", K(ret), KPC(column_schema), K(index_schema));
    } else if (OB_UNLIKELY(is_shadow_column(column_schema->get_column_id()))) {
      continue;// skip the shadow rowkeys for unique index.
    // Generated columns on index table are converted to normal column,
    // we need to get column schema from data table here.
    } else if (OB_ISNULL(index_col = data_table->get_column_schema( column_schema->get_column_id()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get index column schema failed", K(ret), KPC(column_schema));
    } else if (index_col->is_hidden() && index_col->is_generated_column() && !index_col->is_rowkey_column()) {
      // delete the generated column generated internally when the index is created,
      // This kind of generated column is hidden.
      // delete generated column in data table for spatial index
      bool exist_index = false;
      for (int64_t j = 0; OB_SUCC(ret) && !exist_index && j < simple_index_infos.count(); ++j) {
        const ObColumnSchemaV2 *tmp_col = NULL;
        if (simple_index_infos.at(j).table_id_ != index_schema.get_table_id()) {
          // If there are other indexes on the hidden column, they cannot be deleted.
          if (OB_FAIL(schema_guard.get_column_schema(
              simple_index_infos.at(j).table_id_, index_col->get_column_id(), tmp_col))) {
          } else if (tmp_col != NULL) {
            exist_index = true;
          }
        }
      }
      // There are no other indexes, delete the hidden column.
      if (OB_SUCC(ret) && !exist_index) {
        int64_t new_schema_version = OB_INVALID_VERSION;
        if (index_col->is_multivalue_generated_array_column() || index_col->is_multivalue_generated_column()) {
          // multivalue array column not in the index schema, need do delete as well do real delete in drop_inner_generated_domain_extra_column
          if (OB_FAIL(drop_inner_generated_domain_extra_column(trans, data_table, *index_col, new_data_table_schema))) {
          }
        // if generate column is not the last column // 1. update prev_column_id // 2. update inner table
        } else if (OB_FAIL(update_prev_id_for_delete_column(*data_table, new_data_table_schema, *index_col, trans))) {
        } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
        } else if (OB_FAIL(delete_single_column(trans, new_schema_version, new_data_table_schema, index_col->get_column_name_str()))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(alter_table_options(schema_guard,
                                    new_data_table_schema,
                                    *data_table,
                                    false,
                                    trans))) {
    } else {
      for (int64_t j = 0; OB_SUCC(ret) && j < simple_index_infos.count(); ++j) {
        if (simple_index_infos.at(j).table_id_ == index_schema.get_table_id()) {
          simple_index_infos.remove(j);
          if (OB_FAIL(new_data_table_schema.set_simple_index_infos(simple_index_infos))) {
          }
          break;
        }
      }
    }
  }

  return ret;
}

int ObDDLOperator::drop_inner_generated_domain_extra_column(
  common::ObMySQLTransaction &trans,
  const share::schema::ObTableSchema *ori_data_schema,
  const share::schema::ObColumnSchemaV2 &ori_column_schema,
  share::schema::ObTableSchema &new_data_table_schema)
{
  int ret = OB_SUCCESS;

  const ObColumnSchemaV2 *budy_col = NULL;
  bool is_match = false;
  int64_t new_schema_version = OB_INVALID_VERSION;

  if (OB_ISNULL(ori_data_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, ori_data_schema is nullptr", K(ret));
  } else if (ObMulValueIndexBuilderUtil::is_multivalue_array_column(ori_column_schema)) {
  } else if (OB_ISNULL(budy_col = ori_data_schema->get_column_schema( ori_column_schema.get_column_id() + 1))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, budy column schema is nullptr", K(ret), K(ori_column_schema));
  } else if (!ObMulValueIndexBuilderUtil::is_multivalue_array_column(*budy_col)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, budy column schema not fould", K(ret), K(*budy_col));
  } else if (OB_FAIL(ObMulValueIndexBuilderUtil::is_matched_budy_column(ori_column_schema, *budy_col, is_match))) {
  } else if (!is_match) {
    ret = OB_ERR_COLUMN_NOT_FOUND;
    LOG_WARN("unexpected error, budy column not found", K(ret), K(*budy_col));
  // delete budy column
  } else if (OB_FAIL(update_prev_id_for_delete_column(*ori_data_schema, new_data_table_schema, *budy_col, trans))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(delete_single_column(trans, new_schema_version, new_data_table_schema, budy_col->get_column_name_str()))) {
  } else if (OB_FAIL(update_prev_id_for_delete_column(*ori_data_schema, new_data_table_schema, ori_column_schema, trans))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else if (OB_FAIL(delete_single_column(trans, new_schema_version, new_data_table_schema, ori_column_schema.get_column_name_str()))) {
  }

  return ret;
}

// revise column info of check constraints
int ObDDLOperator::revise_constraint_column_info(
    obcall::ObSchemaReviseArg arg,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.csts_array_.count(); ++i) {
      arg.csts_array_.at(i).set_schema_version(new_schema_version);
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(schema_service_.get_runtime_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_table_schema( arg.table_id_, table_schema))) {
    } else if (nullptr == table_schema) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, table schema must not be nullptr", K(ret));
    } else if (OB_FAIL(schema_service->get_table_sql_service().revise_check_cst_column_info(
                trans, *table_schema, arg.csts_array_))) {
    } else if (OB_FAIL(schema_service->get_table_sql_service().update_data_table_schema_version(
               trans, arg.table_id_, table_schema->get_in_offline_ddl_white_list()))) {
    }
  }
  return ret;
}

// revise info of not null constraints
int ObDDLOperator::revise_not_null_constraint_info(
    obcall::ObSchemaReviseArg arg,
    share::schema::ObSchemaGetterGuard &schema_guard,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  const uint64_t table_id = arg.table_id_;
  int64_t new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  const ObTableSchema *ori_table_schema = NULL;
  ObSEArray<const ObColumnSchemaV2 *, 16> not_null_cols;
  const bool update_object_status_ignore_version = false;
  const bool need_del_stats = false;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, ori_table_schema))) {
  } else if (OB_ISNULL(ori_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table schema is null", K(ret), K(table_id));
  } else {
    bool is_table_with_hidden_pk_column = ori_table_schema->is_table_with_hidden_pk_column();
    ObTableSchema::const_column_iterator col_iter = ori_table_schema->column_begin();
    ObTableSchema::const_column_iterator col_iter_end = ori_table_schema->column_end();
    for (; col_iter != col_iter_end && OB_SUCC(ret); col_iter++) {
      if (OB_ISNULL(*col_iter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column schema is null", K(ret), KPC(ori_table_schema));
      } else if (!(*col_iter)->is_nullable() && !(*col_iter)->is_hidden()) {
        // todo@lanyi conisder case when not null columns of order by table
        if (!is_table_with_hidden_pk_column && (*col_iter)->is_rowkey_column()) {
          // do nothing for rowkey columns.
          // not filter rowkey column of no_pk_table since it may be partition key and can be null.
        } else if (OB_UNLIKELY((*col_iter)->has_not_null_constraint())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("column with not null attr should not have not null constraint", K(ret),
                    KPC(*col_iter));
        } else if (OB_FAIL(not_null_cols.push_back(*col_iter))) {
        }
      }
    }
  }

  if (OB_FAIL(ret) || 0 == not_null_cols.count()) {
    // do nothing
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(new_schema_version))) {
  } else {
    uint64_t new_cst_id = OB_INVALID_ID;
    for (int64_t i = 0; OB_SUCC(ret) && i < not_null_cols.count(); ++i) {
      ObArenaAllocator allocator("ReviseNotNulCst");
      ObString cst_name;
      ObString check_expr_str;
      ObConstraint cst;
      const ObColumnSchemaV2 *col_schema = not_null_cols.at(i);
      uint64_t column_id = col_schema->get_column_id();
      bool cst_name_generated = false;
      ObColumnSchemaV2 new_col_schema;
      if (OB_FAIL(ObTableSchema::create_cons_name_automatically_with_dup_check(cst_name,
            ori_table_schema->get_table_name_str(),
            allocator,
            CONSTRAINT_TYPE_NOT_NULL,
            schema_guard,
            ori_table_schema->get_database_id(),
            10, /* retry_times */
            cst_name_generated))) {
      } else if (OB_UNLIKELY(!cst_name_generated)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("duplicate name constraint already exists", K(ret), KPC(ori_table_schema));
      } else if (OB_FAIL(ObResolverUtils::create_not_null_expr_str(
                  col_schema->get_column_name_str(), allocator, check_expr_str))) {
      } else if (OB_FAIL(schema_service->fetch_new_constraint_id(new_cst_id))) {
      } else if (OB_FAIL(new_col_schema.assign(*col_schema))) {
      } else {
        const bool only_history = false;
        const bool need_to_deal_with_cst_cols = true;
        const bool do_cst_revise = true;

        cst.set_table_id(table_id);
        cst.set_constraint_id(new_cst_id);
        cst.set_schema_version(new_schema_version);
        cst.set_constraint_name(cst_name);
        cst.set_name_generated_type(GENERATED_TYPE_SYSTEM);
        cst.set_check_expr(check_expr_str);
        cst.set_constraint_type(CONSTRAINT_TYPE_NOT_NULL);
        cst.set_rely_flag(false);
        cst.set_enable_flag(true);
        cst.set_validate_flag(CST_FK_VALIDATED);

        new_col_schema.set_schema_version(new_schema_version);
        new_col_schema.add_not_null_cst();
        new_col_schema.set_nullable(true);
        if (OB_FAIL(cst.assign_not_null_cst_column_id(column_id))) {
        } else if (OB_FAIL(schema_service->get_table_sql_service().add_single_constraint(
                  trans, cst, only_history,need_to_deal_with_cst_cols, do_cst_revise))) {
        } else if (OB_FAIL(schema_service->get_table_sql_service().update_single_column(
          trans, *ori_table_schema, *ori_table_schema, new_col_schema, false, need_del_stats))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObTableSchema new_table_schema;
      if (OB_FAIL(new_table_schema.assign(*ori_table_schema))) {
      } else {
        new_table_schema.set_schema_version(new_schema_version);
        if (OB_FAIL(schema_service->get_table_sql_service().update_table_attribute(
            trans, new_table_schema, OB_DDL_ADD_CONSTRAINT, update_object_status_ignore_version))) {
        }
      }
    }
  }
  LOG_INFO("revise not null constraint info", K(ret), K(arg));
  return ret;
}

int ObDDLOperator::update_table_status(const ObTableSchema &orig_table_schema,
                                       const int64_t schema_version,
                                       const ObObjectStatus new_status,
                                       const bool update_object_status_ignore_version,
                                       ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  ObTableSchema new_schema;
  const ObSchemaOperationType op = OB_DDL_ALTER_TABLE;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (schema_version <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("schema_version is invalid", K(ret), K(schema_version));
  } else if (OB_FAIL(new_schema.assign(orig_table_schema))) {
  } else if (FALSE_IT(new_schema.set_object_status(new_status))) {
  } else if (FALSE_IT(new_schema.set_schema_version(schema_version))) {
  } else if (new_schema.get_column_count() > 0
             && FALSE_IT(new_schema.set_view_column_filled_flag(ObViewColumnFilledFlag::FILLED))) {
    /*
    *Except for drop view, there is no way to reduce the column count,
    *and there is no need to consider the table mode of this view before
    */
  } else if (OB_FAIL(schema_service->get_table_sql_service().update_table_attribute(trans, new_schema, op, update_object_status_ignore_version) )) {
  }
  return ret;
}

int ObDDLOperator::update_view_columns(const ObTableSchema &view_schema,
                                        common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service->get_table_sql_service().update_view_columns(trans, view_schema))) {
  }
  return ret;
}

// only used in upgrading
int ObDDLOperator::reset_view_status(common::ObMySQLTransaction &trans,
                                     const ObTableSchema *table)
{
  int ret = OB_SUCCESS;
  ObObjectStatus new_status = ObObjectStatus::INVALID;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  int64_t schema_version = OB_INVALID_VERSION;
  const bool update_object_status_ignore_version = true;
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else {
    if (OB_ISNULL(table) || !table->is_view_table()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get wrong schema", K(ret), KP(table));
    } else if (OB_FAIL(schema_service->gen_new_schema_version(schema_version, schema_version))) {
    } else if (OB_FAIL(update_table_status(*table,
                                            schema_version,
                                            new_status,
                                            update_object_status_ignore_version,
                                            trans))) {
    }
  }
  return ret;
}

int ObDDLOperator::exchange_table_partitions(const share::schema::ObTableSchema &orig_table_schema,
                                             share::schema::ObTableSchema &inc_table_schema,
                                             share::schema::ObTableSchema &del_table_schema,
                                             common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  int64_t drop_new_schema_version = OB_INVALID_VERSION;
  int64_t add_new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(drop_new_schema_version))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(add_new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().exchange_part_info(
                     trans,
                     orig_table_schema,
                     inc_table_schema,
                     del_table_schema,
                     drop_new_schema_version,
                     add_new_schema_version))) {
  }
  return ret;
}

int ObDDLOperator::exchange_table_subpartitions(const share::schema::ObTableSchema &orig_table_schema,
                                                share::schema::ObTableSchema &inc_table_schema,
                                                share::schema::ObTableSchema &del_table_schema,
                                                common::ObMySQLTransaction &trans,
                                                const bool is_subpart_idx_specified)
{
  int ret = OB_SUCCESS;

  int64_t drop_new_schema_version = OB_INVALID_VERSION;
  int64_t add_new_schema_version = OB_INVALID_VERSION;
  ObSchemaService *schema_service = schema_service_.get_schema_service();
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is NULL", K(ret));
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(drop_new_schema_version))) {
  } else if (OB_FAIL(schema_service_.gen_new_schema_version(add_new_schema_version))) {
  } else if (OB_FAIL(schema_service->get_table_sql_service().exchange_subpart_info(
                     trans,
                     orig_table_schema,
                     inc_table_schema,
                     del_table_schema,
                     drop_new_schema_version,
                     add_new_schema_version,
                     is_subpart_idx_specified))) {
  }
  return ret;
}

int ObDDLOperator::get_target_auto_inc_sequence_value(const uint64_t table_id,
                                                      const uint64_t column_id,
                                                      uint64_t &sequence_value,
                                                      common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  sequence_value = OB_INVALID_ID;
  if (OB_UNLIKELY(OB_INVALID_ID == table_id || OB_INVALID_ID == column_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id), K(column_id));
  } else {
    ObSqlString sql;

    const char *table_name = OB_ALL_AUTO_INCREMENT_TNAME;
    if (OB_FAIL(sql.assign_fmt(" SELECT  sequence_value FROM %s WHERE sequence_key = %lu"
                               " AND column_id = %lu FOR UPDATE",
                               table_name,
                               ObSchemaUtils::get_extract_schema_id(table_id),
                               column_id))) {
    } else {
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        common::sqlclient::ObMySQLResult *result = NULL;
        uint64_t sequence_table_id = OB_ALL_AUTO_INCREMENT_TID;
        if (OB_FAIL(trans.read(res, sql.ptr()))) {
        } else if (NULL == (result = res.get_result())) {
          LOG_WARN("failed to get result", K(ret));
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(result->next())) {
          LOG_WARN("failed to get next", K(ret));
          if (OB_ITER_END == ret) {
            // auto-increment column has been deleted
            ret = OB_SCHEMA_ERROR;
            LOG_WARN("failed to get next", K(ret));
          }
        } else if (OB_FAIL(result->get_uint("sequence_value", sequence_value))) {
        }
        if (OB_SUCC(ret)) {
          int tmp_ret = OB_SUCCESS;
          if (OB_ITER_END != (tmp_ret = result->next())) {
            if (OB_SUCCESS == tmp_ret) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("more than one row", K(ret), K(table_id), K(column_id));
            } else {
              ret = tmp_ret;
              LOG_WARN("fail to iter next row", K(ret), K(table_id), K(column_id));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLOperator::set_target_auto_inc_sync_value(const uint64_t table_id,
                                                  const uint64_t column_id,
                                                  const uint64_t new_sequence_value,
                                                  const uint64_t new_sync_value,
                                                  common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == table_id || OB_INVALID_ID == column_id || new_sequence_value < 0 || new_sync_value < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id), K(column_id), K(new_sequence_value), K(new_sync_value));
  } else {
    ObSqlString sql;
    int64_t affected_rows = 0;
    const char *table_name = OB_ALL_AUTO_INCREMENT_TNAME;
    if (OB_FAIL(sql.assign_fmt(
                "UPDATE %s SET sequence_value = %lu, sync_value = %lu WHERE sequence_key=%lu AND column_id=%lu",
                table_name, new_sequence_value, new_sync_value,
                ObSchemaUtils::get_extract_schema_id(table_id), column_id))) {
    } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
    }
  }
  return ret;
}


}//end namespace rootserver
}//end namespace oceanbase

// modify_all_obj_status / update_max_dependency_version are owned by
// rootserver::ObDependencyDDLHelper.
