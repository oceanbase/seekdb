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
#include "rootserver/parallel_ddl/ob_table_helper.h"
#include "rootserver/ob_index_builder.h"
#include "rootserver/ob_lob_meta_builder.h"
#include "rootserver/ob_lob_piece_builder.h"
#include "rootserver/ob_table_creator.h"
#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_debug_sync_point.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "sql/engine/cmd/ob_partition_executor_utils.h"
#include "share/schema/ob_table_sql_service.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "sql/resolver/ddl/ob_index_builder_util.h"
#include "sql/resolver/ob_resolver_utils.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;



ObTableHelper::ObTableHelper(
  share::schema::ObMultiVersionSchemaService *schema_service,
  const char* parallel_ddl_type,
  ObDDLSQLTransaction *external_trans,
  bool enable_ddl_parallel)
  : new_tables_(),
    new_mock_fk_parent_tables_()
{}

int ObTableHelper::try_replace_mock_fk_parent_table_(
                   const uint64_t replace_mock_fk_parent_table_id,
                   ObMockFKParentTableSchema *&new_mock_fk_parent_table)
{
  int ret = OB_SUCCESS;
  new_mock_fk_parent_table = NULL;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_INVALID_ID == replace_mock_fk_parent_table_id) {
    // do nothing
  } else if (OB_UNLIKELY(new_tables_.count() <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected table cnt", KR(ret), K(new_tables_.count()));
  } else {
    // check if data table's columns are matched with existed mock fk parent table
    const ObTableSchema &data_table = new_tables_.at(0);

    ObArray<const share::schema::ObTableSchema*> index_schemas;
    for (int64_t i = 1; OB_SUCC(ret) && i < new_tables_.count(); ++i) {
      if (new_tables_.at(i).is_unique_index()
          && OB_FAIL(index_schemas.push_back(&new_tables_.at(i)))) {
        LOG_WARN("failed to push back to index_schemas", KR(ret));
      }
    } // end for

    const ObMockFKParentTableSchema *mock_fk_parent_table = NULL;
    if (FAILEDx(schema_guard_wrapper_.get_mock_fk_parent_table_schema(
        replace_mock_fk_parent_table_id, mock_fk_parent_table))) {
      LOG_WARN("fail to get mock fk parent table schema",
               KR(ret), K(replace_mock_fk_parent_table_id));
    } else if (OB_ISNULL(mock_fk_parent_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("mock fk parent table not exist",
               KR(ret), K(replace_mock_fk_parent_table_id));
    } else if (OB_FAIL(check_fk_columns_type_for_replacing_mock_fk_parent_table_(
               data_table, *mock_fk_parent_table))) {
    } else if (OB_FAIL(ObSchemaUtils::alloc_schema(
               allocator_, *mock_fk_parent_table, new_mock_fk_parent_table))) {
    } else {
      (void) new_mock_fk_parent_table->set_operation_type(
             ObMockFKParentTableOperationType::MOCK_FK_PARENT_TABLE_OP_REPLACED_BY_REAL_PREANT_TABLE);
      const ObIArray<ObForeignKeyInfo> &ori_mock_fk_infos_array = mock_fk_parent_table->get_foreign_key_infos();
      // modify the parent column id of fk，make it fit with real parent table
      // mock_column_id -> column_name -> real_column_id
      for (int64_t i = 0; OB_SUCC(ret) && i < ori_mock_fk_infos_array.count(); ++i) {
        const ObForeignKeyInfo &ori_foreign_key_info = mock_fk_parent_table->get_foreign_key_infos().at(i);
        ObForeignKeyInfo &new_foreign_key_info = new_mock_fk_parent_table->get_foreign_key_infos().at(i);
        new_foreign_key_info.parent_column_ids_.reuse();
        new_foreign_key_info.fk_ref_type_ = FK_REF_TYPE_INVALID;
        new_foreign_key_info.is_parent_table_mock_ = false;
        new_foreign_key_info.parent_table_id_ = data_table.get_table_id();
        // replace parent table columns
        for (int64_t j = 0;  OB_SUCC(ret) && j < ori_foreign_key_info.parent_column_ids_.count(); ++j) {
          bool is_column_exist = false;
          uint64_t mock_parent_table_column_id = ori_foreign_key_info.parent_column_ids_.at(j);
          ObString column_name;
          const ObColumnSchemaV2 *col_schema = NULL;
          (void) mock_fk_parent_table->get_column_name_by_column_id(
                 mock_parent_table_column_id, column_name, is_column_exist);
          if (!is_column_exist) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("column is not exist", KR(ret), K(mock_parent_table_column_id), KPC(mock_fk_parent_table));
          } else if (OB_ISNULL(col_schema = data_table.get_column_schema(column_name))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get column schema failed", KR(ret), K(column_name));
          } else if (OB_FAIL(new_foreign_key_info.parent_column_ids_.push_back(col_schema->get_column_id()))) {
          }
        } // end for
        // check and mofidy ref cst type and ref cst id of fk
        const ObRowkeyInfo &rowkey_info = data_table.get_rowkey_info();
        common::ObArray<uint64_t> pk_column_ids;
        for (int64_t j = 0; OB_SUCC(ret) && j < rowkey_info.get_size(); ++j) {
          uint64_t column_id = 0;
          const ObColumnSchemaV2 *col_schema = NULL;
          if (OB_FAIL(rowkey_info.get_column_id(j, column_id))) {
          } else if (OB_ISNULL(col_schema = data_table.get_column_schema(column_id))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get index column schema failed", KR(ret));
          } else if (col_schema->is_hidden() || col_schema->is_shadow_column()) {
            // do nothing
          } else if (OB_FAIL(pk_column_ids.push_back(col_schema->get_column_id()))) {
          }
        } // end for
        bool is_match = false;
        if (FAILEDx(sql::ObResolverUtils::check_match_columns(
            pk_column_ids, new_foreign_key_info.parent_column_ids_, is_match))) {
          LOG_WARN("check_match_columns failed", KR(ret));
        } else if (is_match) {
          new_foreign_key_info.fk_ref_type_ = FK_REF_TYPE_PRIMARY_KEY;
        } else { // pk is not match, check if uk match
          if (OB_FAIL(ddl_service_->get_uk_cst_id_for_replacing_mock_fk_parent_table(
              index_schemas, new_foreign_key_info))) {
          } else if (FK_REF_TYPE_INVALID == new_foreign_key_info.fk_ref_type_) {
            ret = OB_ERR_CANNOT_ADD_FOREIGN;
            LOG_WARN("fk_ref_type is invalid", KR(ret), KPC(mock_fk_parent_table));
          }
        }
      }
    }
  }
  return ret;
}

int ObTableHelper::check_fk_columns_type_for_replacing_mock_fk_parent_table_(
    const ObTableSchema &parent_table_schema,
    const ObMockFKParentTableSchema &mock_parent_table_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < mock_parent_table_schema.get_foreign_key_infos().count(); ++i) {
    const ObTableSchema *child_table_schema = NULL;
    const ObForeignKeyInfo &fk_info = mock_parent_table_schema.get_foreign_key_infos().at(i);
    if (OB_FAIL(schema_guard_wrapper_.get_table_schema(fk_info.child_table_id_, child_table_schema))) {
    } else if (OB_ISNULL(child_table_schema)) {
      ret = OB_ERR_PARALLEL_DDL_CONFLICT;
      LOG_WARN("child table schema is null, need retry", KR(ret), K(fk_info));
    } else {
      // prepare params for check_foreign_key_columns_type
      ObArray<ObString> child_columns;
      ObArray<ObString> parent_columns;
      bool is_column_exist = false;
      for (int64_t j = 0; OB_SUCC(ret) && j < fk_info.child_column_ids_.count(); ++j) {
        ObString child_column_name;
        const ObColumnSchemaV2 *child_col = child_table_schema->get_column_schema(fk_info.child_column_ids_.at(j));
        if (OB_ISNULL(child_col)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("column is not exist", KR(ret), K(fk_info));
        } else if (OB_FAIL(child_columns.push_back(child_col->get_column_name_str()))) {
        }
      } // end for
      for (int64_t j = 0; OB_SUCC(ret) && j < fk_info.parent_column_ids_.count(); ++j) {
        ObString parent_column_name;
        (void) mock_parent_table_schema.get_column_name_by_column_id(
               fk_info.parent_column_ids_.at(j), parent_column_name, is_column_exist);
        if (!is_column_exist) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("column is not exist", KR(ret), K(fk_info));
        } else if (OB_FAIL(parent_columns.push_back(parent_column_name))) {
        }
      } // end for
      if (FAILEDx(sql::ObResolverUtils::check_foreign_key_columns_type(
          *child_table_schema,
          parent_table_schema,
          child_columns,
          parent_columns,
          NULL))) {
        ret = OB_ERR_CANNOT_ADD_FOREIGN;
        LOG_WARN("Failed to check_foreign_key_columns_type", KR(ret));
      }
    }
  } // end for
  return ret;
}

int ObTableHelper::create_tables_(const ObString *ddl_stmt_str)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(schema_service_impl = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service impl is null", KR(ret));
  } else {
    int64_t new_schema_version = OB_INVALID_VERSION;
    for (int64_t i = 0; OB_SUCC(ret) && i < new_tables_.count(); i++) {
      ObTableSchema &new_table = new_tables_.at(i);
      if (OB_FAIL(ObFtsIndexBuilderUtil::try_load_and_lock_dictionary_tables(new_table, get_trans_()))) {
      } else if (OB_FAIL(schema_service_->gen_new_schema_version(new_schema_version))) {
      } else if (FALSE_IT(new_table.set_schema_version(new_schema_version))) {
      } else if (new_table.is_vec_delta_buffer_type() &&
                 OB_FAIL(ObVectorIndexUtil::add_dbms_vector_jobs(get_trans_(),
                                                                 new_table.get_table_id(),
                                                                 new_table.get_exec_env()))) {
        LOG_WARN("failed to add dbms_vector jobs", KR(ret), K(new_table));
      }
    } // end for
    if (FAILEDx(schema_service_impl->get_table_sql_service().batch_create_table(new_tables_,
                get_trans_(),
                ddl_stmt_str,
                true/*sync_schema_version_for_last_table*/))) {
      LOG_WARN("failed to batch create table", KR(ret), K_(new_tables), KPC(ddl_stmt_str));
    }
  }
  return ret;
}

int ObTableHelper::deal_with_mock_fk_parent_tables_(const uint64_t replace_mock_fk_parent_table_id)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service_impl = NULL;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(schema_service_impl = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service impl is null", KR(ret));
  } else if (OB_UNLIKELY(new_tables_.count() <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected table cnt", KR(ret), K(new_tables_.count()));
  } else {
    const ObTableSchema &data_table = new_tables_.at(0);
    const uint64_t data_table_id = data_table.get_table_id();
    int64_t new_schema_version = OB_INVALID_VERSION;
    for (int64_t i = 0; OB_SUCC(ret) && i < new_mock_fk_parent_tables_.count(); i++) {
      ObMockFKParentTableSchema *new_mock_fk_parent_table = new_mock_fk_parent_tables_.at(i);
      if (OB_ISNULL(new_mock_fk_parent_table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("mock fk parent table is null", KR(ret));
      } else if (OB_FAIL(schema_service_->gen_new_schema_version(new_schema_version))) {
      } else {
        new_mock_fk_parent_table->set_schema_version(new_schema_version);
        ObMockFKParentTableOperationType operation_type = new_mock_fk_parent_table->get_operation_type();
        if (MOCK_FK_PARENT_TABLE_OP_CREATE_TABLE_BY_ADD_FK_IN_CHILD_TBALE == operation_type) {
          // 1. create table: mock fk parent table doesn't exist.
          if (OB_FAIL(schema_service_impl->get_table_sql_service().add_mock_fk_parent_table(
              &get_trans_(), *new_mock_fk_parent_table, false /*need_update_foreign_key*/))) {
          }
        } else if (MOCK_FK_PARENT_TABLE_OP_UPDATE_SCHEMA_VERSION == operation_type
                   || MOCK_FK_PARENT_TABLE_OP_ADD_COLUMN == operation_type) {
          // 2. alter table: mock fk parent table has new child table.
          if (OB_FAIL(schema_service_impl->get_table_sql_service().alter_mock_fk_parent_table(
                      &get_trans_(), *new_mock_fk_parent_table))) {
          }
        } else if (MOCK_FK_PARENT_TABLE_OP_REPLACED_BY_REAL_PREANT_TABLE == operation_type) {
          // 3. replace table: replace existed mock fk parent table with data table
          const ObMockFKParentTableSchema *ori_mock_fk_parent_table = NULL;
          if (OB_UNLIKELY(OB_INVALID_ID == replace_mock_fk_parent_table_id)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("invalid replace mock fk parent table id", KR(ret), K(replace_mock_fk_parent_table_id));
          } else if (OB_FAIL(schema_guard_wrapper_.get_mock_fk_parent_table_schema(
            replace_mock_fk_parent_table_id, ori_mock_fk_parent_table))) {
          } else if (OB_ISNULL(ori_mock_fk_parent_table)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("mock fk parent table not exist, unexpected",
                     KR(ret), K(replace_mock_fk_parent_table_id));
          } else {
            // 3.1. drop mock fk parent table.
            // 3.2. update foreign keys from mock fk parent table.
            if (OB_FAIL(schema_service_impl->get_table_sql_service().replace_mock_fk_parent_table(
                        &get_trans_(), *new_mock_fk_parent_table, ori_mock_fk_parent_table))) {
            }

            // 3.3. update child new_tables_' schema version.
            for (int64_t j = 0; OB_SUCC(ret) && j < new_mock_fk_parent_table->get_foreign_key_infos().count(); j++) {
              const ObForeignKeyInfo &foreign_key = new_mock_fk_parent_table->get_foreign_key_infos().at(j);
              const uint64_t child_table_id = foreign_key.child_table_id_;
              const ObTableSchema *child_table = NULL;
              if (OB_FAIL(schema_guard_wrapper_.get_table_schema(child_table_id, child_table))) {
              } else if (OB_ISNULL(child_table)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("child table is not exist", KR(ret), K(child_table_id));
              } else if (OB_FAIL(schema_service_impl->get_table_sql_service().update_data_table_schema_version(
                                 get_trans_(), child_table_id, child_table->get_in_offline_ddl_white_list()))) {
              }
            } // end for

            // 3.4. update data table's schema version at last.
            if (FAILEDx(schema_service_impl->get_table_sql_service().update_data_table_schema_version(
                        get_trans_(), data_table_id, false/*in_offline_ddl_white_list*/))) {
              LOG_WARN("fail to update data table's schema version", KR(ret), K(data_table_id));
            }
          }
        } else {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("mock fk parent table operation type is not supported", KR(ret), K(operation_type));
        }
      }
    } // end for
  }
  return ret;
}

int ObTableHelper::create_tablets_()
{
  int ret = OB_SUCCESS;
  SCN frozen_scn;
  ObSchemaGetterGuard schema_guard;
  ObSchemaService *schema_service_impl = NULL;
  const uint64_t data_format_version = DATA_CURRENT_VERSION;

  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_ISNULL(schema_service_impl = schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service impl is null", KR(ret));
  } else if (OB_FAIL(ObMajorFreezeHelper::get_frozen_scn(frozen_scn, &get_trans_()))) {
  } else if (OB_FAIL(schema_service_->get_runtime_schema_guard(schema_guard))) {
  } else if (OB_UNLIKELY(new_tables_.count() <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected table cnt", KR(ret), K(new_tables_.count()));
  } else {
    const ObTableSchema &data_table = new_tables_.at(0);
    ObTableCreator table_creator(
                   frozen_scn,
                   get_trans_());

    // use the external_trans as sql_proxy if not null,
    // to ensure that changes in the current DDL transaction can be queried
    common::ObISQLClient *sql_proxy = get_external_trans_();
    if (sql_proxy == NULL) {
      sql_proxy = sql_proxy_;
    }
    int64_t last_schema_version = OB_INVALID_VERSION;
    ObSchemaVersionGenerator *tsi_generator = GET_TSI(TSISchemaVersionGenerator);
    if (OB_FAIL(table_creator.init(true/*need_tablet_cnt_check*/))) {
    } else if (OB_ISNULL(tsi_generator)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tsi schema version generator is null", KR(ret));
    } else if (OB_FAIL(tsi_generator->get_current_version(last_schema_version))) {
    } else if (OB_UNLIKELY(last_schema_version <= 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("last schema version is invalid", KR(ret), K(last_schema_version));
    }
    if (OB_FAIL(ret)) {
    } else {
      ObArray<const ObTableSchema*> schemas;
      ObArray<bool> need_create_empty_majors;
      ObArray<uint64_t> table_ids;
      for (int64_t i = 0; OB_SUCC(ret) && i < new_tables_.count(); i++) {
        const ObTableSchema &new_table = new_tables_.at(i);
        const uint64_t table_id = new_table.get_table_id();
        if (!new_table.has_tablet()) {
          // eg. external table ...
        } else if (!new_table.is_global_index_table()) {
          if (OB_FAIL(schemas.push_back(&new_table))) {
          } else if (OB_FAIL(need_create_empty_majors.push_back(true))) {
          }
        } else {
          if (OB_FAIL(table_creator.add_create_tablets_of_table_arg(
                     new_table, data_format_version, true/*need create major sstable*/))) {
          }
        }
        if (FAILEDx(table_ids.push_back(table_id))) {
          LOG_WARN("failed to push_back table_id", KR(ret), K(table_id));
        }
      } // end for

      if (FAILEDx(schema_service_impl->get_table_sql_service().batch_insert_ori_schema_version(
                                       get_trans_(), table_ids, last_schema_version))) {
        LOG_WARN("failed to batch insert ori schema version", KR(ret), K(table_ids),
                                                              K(last_schema_version));
      } else if (schemas.count() > 0) {
        if (OB_FAIL(table_creator.add_create_tablets_of_tables_arg(
                   schemas, data_format_version, need_create_empty_majors /*need create major sstable*/))) {
        } else if (OB_FAIL(table_creator.execute())) {
        }
      }
    }
  }
  RS_TRACE(create_tablets);
  return ret;
}


int ObTableHelper::calc_schema_version_cnt_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_UNLIKELY(new_tables_.count() <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected table cnt", KR(ret), K(new_tables_.count()));
  } else {
    const ObTableSchema &data_table = new_tables_.at(0);
    // 0. data table
    schema_version_cnt_ = 1; // init

    // 3. create index/lob table
    if (new_tables_.count() > 1) {
      schema_version_cnt_ += (new_tables_.count() - 1);
      // update data table schema version
      schema_version_cnt_++;
    }

    // 2. foreign key (without mock fk parent table)

    // this logic is duplicated because of add_foreign_key() will also update data table's schema_version.
    // schema_version_cnt_ += data_table.get_depend_table_ids();
    const ObIArray<ObForeignKeyInfo> &foreign_key_infos = data_table.get_foreign_key_infos();
    for (int64_t i = 0; OB_SUCC(ret) && i < foreign_key_infos.count(); i++) {
      const ObForeignKeyInfo &foreign_key_info = foreign_key_infos.at(i);
      if (foreign_key_info.is_modify_fk_state_) {
        continue;
      } else if (!foreign_key_info.is_parent_table_mock_) {
        schema_version_cnt_++;
        // TODO(yanmu.ztl): can be optimized in the following cases:
        // - self reference
        // - foreign keys has same parent table.
      }
    } // end for

    // 5. mock fk parent table
    // schema version for new mock fk parent tables
    schema_version_cnt_ += new_mock_fk_parent_tables_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < new_mock_fk_parent_tables_.count(); i++) {
      const ObMockFKParentTableSchema *new_mock_fk_parent_table = new_mock_fk_parent_tables_.at(i);
      if (OB_ISNULL(new_mock_fk_parent_table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("mock fk parent table is null", KR(ret));
      } else if (MOCK_FK_PARENT_TABLE_OP_CREATE_TABLE_BY_ADD_FK_IN_CHILD_TBALE
                 == new_mock_fk_parent_table->get_operation_type()) {
        // skip
      } else if (MOCK_FK_PARENT_TABLE_OP_UPDATE_SCHEMA_VERSION
                 == new_mock_fk_parent_table->get_operation_type()
                 || MOCK_FK_PARENT_TABLE_OP_ADD_COLUMN
                 == new_mock_fk_parent_table->get_operation_type()) {
        // update new mock fk parent table(is useless here, just to be compatible with other logic)
        schema_version_cnt_++;
      } else if (MOCK_FK_PARENT_TABLE_OP_REPLACED_BY_REAL_PREANT_TABLE
                 == new_mock_fk_parent_table->get_operation_type()) {
        // update foreign keys' schema version.
        schema_version_cnt_++;
        // update child tables' schema version.
        // TODO(yanmu.ztl): can be optimized when child table is duplicated.
        schema_version_cnt_ += (new_mock_fk_parent_table->get_foreign_key_infos().count());
        // update data table's schema version at last.
        schema_version_cnt_++;
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not supported operation type", KR(ret), KPC(new_mock_fk_parent_table));
      }
    } // end for

    // 6. for 1503 boundary ddl operation
    schema_version_cnt_++;
  }
  return ret;
}

int ObTableHelper::create_schemas_(const ObString *ddl_stmt_str,
                                   const uint64_t replace_mock_fk_parent_table_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_FAIL(create_tables_(ddl_stmt_str))) {
  } else if (OB_FAIL(deal_with_mock_fk_parent_tables_(replace_mock_fk_parent_table_id))) {
  }
  RS_TRACE(operate_schemas);
  return ret;
}


void ObTableHelper::adjust_create_if_not_exist_(int &ret, bool if_not_exist, bool &do_nothing)
{
  if (OB_ERR_TABLE_EXIST == ret) {
    //create table if not exist xx like (...)
    if (if_not_exist) {
      do_nothing = true;
      ret = OB_SUCCESS;
    }
  }
}

int ObTableHelper::inner_create_table_(const ObString *ddl_stmt_str,
                                       const uint64_t replace_mock_fk_parent_table_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_FAIL(create_schemas_(ddl_stmt_str,
                                     replace_mock_fk_parent_table_id))) {
  } else if (OB_FAIL(create_tablets_())) {
  }
  return ret;
}

int ObTableHelper::generate_schemas_()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_FAIL(generate_table_schema_())) {
  } else if (OB_FAIL(generate_aux_table_schemas_())) {
  } else if (OB_FAIL(gen_partition_object_and_tablet_ids_(new_tables_))) {
  } else if (OB_FAIL(generate_foreign_keys_())) {
  }
  return ret;
}

int ObTableHelper::inner_generate_table_schema_(const ObCreateTableArg &arg, ObTableSchema &new_table)
{
  int ret = OB_SUCCESS;
  const uint64_t mock_table_id = OB_MIN_USER_OBJECT_ID + 1;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_UNLIKELY(OB_INVALID_ID != arg.schema_.get_table_id())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("create table with table_id in 4.x is not supported",
             KR(ret), "table_id", arg.schema_.get_table_id());
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "create table with id is");
  }

  if (FALSE_IT(new_table.set_table_id(mock_table_id))) {
  } else if (FAILEDx(ddl_service_->try_format_partition_schema(new_table))) {
    LOG_WARN("fail to format partition schema", KR(ret));
  }

  if (FAILEDx(check_table_udt_exist_(new_table))) {
    LOG_WARN("fail to check table udt exist", KR(ret));
  }

  // check if constraint name duplicated
  const uint64_t database_id = new_table.get_database_id();
  bool cst_exist = false;
  const ObIArray<ObConstraint> &constraints = arg.constraint_list_;
  const int64_t cst_cnt = constraints.count();
  for (int64_t i = 0; OB_SUCC(ret) && i < cst_cnt; i++) {
    const ObConstraint &cst = constraints.at(i);
    const ObString &cst_name = cst.get_constraint_name_str();
    if (OB_UNLIKELY(cst.get_constraint_name_str().empty())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cst name is empty", KR(ret), K(database_id), K(cst_name));
    } else if (OB_FAIL(check_constraint_name_exist_(new_table, cst_name, false /*is_foreign_key*/, cst_exist))) {
    } else if (cst_exist) {
      ret = OB_ERR_CONSTRAINT_NAME_DUPLICATE;
      LOG_USER_ERROR(OB_ERR_CONSTRAINT_NAME_DUPLICATE, cst_name.length(), cst_name.ptr());
      LOG_WARN("cst name is duplicate", KR(ret), K(database_id), K(cst_name));
    }
  } // end for

  // fetch object_ids (data table + constraints)
  ObIDGenerator id_generator;
  const uint64_t object_cnt = cst_cnt + 1;
  uint64_t object_id = OB_INVALID_ID;
  if (FAILEDx(gen_object_ids_(object_cnt, id_generator))) {
    LOG_WARN("fail to gen object ids", KR(ret), K(object_cnt));
  } else if (OB_FAIL(id_generator.next(object_id))) {
  } else {
    (void) new_table.set_table_id(object_id);
  }

  // generate constraints
  for (int64_t i = 0; OB_SUCC(ret) && i < cst_cnt; i++) {
    ObConstraint &cst = const_cast<ObConstraint &>(constraints.at(i));

    cst.set_table_id(new_table.get_table_id());
    if (OB_FAIL(id_generator.next(object_id))) {
    } else if (FALSE_IT(cst.set_constraint_id(object_id))) {
    } else if (OB_FAIL(new_table.add_constraint(cst))) {
    }
  } // end for

  return ret;
}

int ObTableHelper::inner_generate_aux_table_schema_(const ObCreateTableArg &arg)
{
  int ret = OB_SUCCESS;
  HEAP_VAR(ObTableSchema, index_schema) {
  ObTableSchema *data_table = nullptr;
  if (OB_FAIL(check_inner_stat_())) {
  } else if (OB_UNLIKELY(new_tables_.count() <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table cnt not match", KR(ret), "table_cnt", new_tables_.count());
  } else {
    ObTableSchema *data_table = &(new_tables_.at(0));
    if (OB_ISNULL(data_table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("data table is nullptr", KR(ret));
    }
    if (OB_SUCC(ret)) {
      data_table = &(new_tables_.at(0));
      // 0. fetch object_ids
      ObIDGenerator id_generator;
      int64_t object_cnt = arg.index_arg_list_.size();
      bool has_lob_table = false;
      uint64_t object_id = OB_INVALID_ID;
      has_lob_table = data_table->has_lob_column(true/*ignore_unused_column*/);
      if (has_lob_table) {
        object_cnt += 2;
      }
      if (FAILEDx(gen_object_ids_(object_cnt, id_generator))) {
        LOG_WARN("fail to gen object ids", KR(ret), K(object_cnt));
      }

      // 1. build index table
      ObIndexBuilder index_builder(*ddl_service_);
      for (int64_t i = 0; OB_SUCC(ret) && i < arg.index_arg_list_.size(); ++i) {
        index_schema.reset();
        obcall::ObCreateIndexArg &index_arg = const_cast<obcall::ObCreateIndexArg&>(arg.index_arg_list_.at(i));
        if (!index_arg.index_schema_.is_partitioned_table()
            && !data_table->is_partitioned_table()) {
          if (INDEX_TYPE_NORMAL_GLOBAL == index_arg.index_type_) {
            index_arg.index_type_ = INDEX_TYPE_NORMAL_GLOBAL_LOCAL_STORAGE;
          } else if (INDEX_TYPE_UNIQUE_GLOBAL == index_arg.index_type_) {
            index_arg.index_type_ = INDEX_TYPE_UNIQUE_GLOBAL_LOCAL_STORAGE;
          } else if (INDEX_TYPE_SPATIAL_GLOBAL == index_arg.index_type_) {
            index_arg.index_type_ = INDEX_TYPE_SPATIAL_GLOBAL_LOCAL_STORAGE;
          } else if (is_global_fts_index(index_arg.index_type_)) {
            if (index_arg.index_type_ == INDEX_TYPE_DOC_ID_ROWKEY_GLOBAL) {
              index_arg.index_type_ = INDEX_TYPE_DOC_ID_ROWKEY_GLOBAL_LOCAL_STORAGE;
            } else if (index_arg.index_type_ == INDEX_TYPE_FTS_INDEX_GLOBAL) {
              index_arg.index_type_ = INDEX_TYPE_FTS_INDEX_GLOBAL_LOCAL_STORAGE;
            } else if (index_arg.index_type_ == INDEX_TYPE_FTS_DOC_WORD_GLOBAL) {
              index_arg.index_type_ = INDEX_TYPE_FTS_DOC_WORD_GLOBAL_LOCAL_STORAGE;
            }
          }
        }
        // the global index has generated column schema during resolve, RS no need to generate index schema,
        // just assign column schema
        if (INDEX_TYPE_NORMAL_GLOBAL == index_arg.index_type_
            || INDEX_TYPE_UNIQUE_GLOBAL == index_arg.index_type_
            || INDEX_TYPE_SPATIAL_GLOBAL == index_arg.index_type_) {
          if (OB_FAIL(index_schema.assign(index_arg.index_schema_))) {
          }
        }
        const bool global_index_without_column_info = false;
        ObSEArray<ObColumnSchemaV2 *, 1> gen_columns;
        ObIAllocator *allocator = index_arg.index_schema_.get_allocator();
        if (OB_FAIL(ret)) {
        } else if (OB_ISNULL(allocator)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid allocator", KR(ret));
        } else if (OB_FAIL(ObIndexBuilderUtil::adjust_expr_index_args(
                   index_arg, *data_table, *allocator, gen_columns))) {
        } else if (OB_FAIL(index_builder.generate_schema(index_arg,
                                                         *data_table,
                                                         global_index_without_column_info,
                                                         false, /*generate_id*/
                                                         index_schema))) {
        } else if (OB_FAIL(id_generator.next(object_id))) {
        } else if (FALSE_IT(index_schema.set_table_id(object_id))) {
        } else if (OB_FAIL(index_schema.generate_origin_index_name())) {
        } else if (OB_FAIL(new_tables_.push_back(index_schema))) {
        } else {
          data_table = &(new_tables_.at(0)); // memory of data table may change after add table to new_tables_
        }
      } // end for

      // 2. build lob table
      if (OB_SUCC(ret) && has_lob_table) {
        HEAP_VARS_2((ObTableSchema, lob_meta_schema), (ObTableSchema, lob_piece_schema)) {
        ObLobMetaBuilder lob_meta_builder(*ddl_service_);
        ObLobPieceBuilder lob_piece_builder(*ddl_service_);
        bool need_object_id = false;
        if (OB_FAIL(id_generator.next(object_id))) {
        } else if (OB_FAIL(lob_meta_builder.generate_aux_lob_meta_schema(
          schema_service_->get_schema_service(), *data_table, object_id, lob_meta_schema, need_object_id))) {
        } else if (OB_FAIL(id_generator.next(object_id))) {
        } else if (OB_FAIL(lob_piece_builder.generate_aux_lob_piece_schema(
          schema_service_->get_schema_service(), *data_table, object_id, lob_piece_schema, need_object_id))) {
        } else if (OB_FAIL(new_tables_.push_back(lob_meta_schema))) {
        } else if (OB_FAIL(new_tables_.push_back(lob_piece_schema))) {
        } else {
          data_table = &(new_tables_.at(0)); // memory of data table may change after add table to new_tables_
          data_table->set_aux_lob_meta_tid(lob_meta_schema.get_table_id());
          data_table->set_aux_lob_piece_tid(lob_piece_schema.get_table_id());
        }

        } // end HEAP_VARS_2
      }
    }
  }

  } // end HEAP_VAR
  return ret;
}
