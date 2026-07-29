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
#include "ob_schema_utils.h"
#include "share/config/ob_runtime_config.h"  // RUNTIME_CONF/ObRuntimeConfigGuard(both are in share/config)
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/ob_server_struct.h"
namespace oceanbase
{
using namespace common;
using namespace sql;
namespace share
{
namespace schema
{




uint64_t ObSchemaUtils::get_extract_schema_id(const uint64_t schema_id)
{
  UNUSED(1UL);
  return schema_id;
}

int ObSchemaUtils::get_all_table_name(
    const char* &table_name,
    const ObServerSchemaService *schema_service /*=NULL*/)
{
  int ret = OB_SUCCESS;
  UNUSEDx(schema_service);
  table_name = OB_ALL_TABLE_TNAME;
  return ret;
}

int ObSchemaUtils::get_all_table_history_name(const char* &table_name,
    const ObServerSchemaService *schema_service /*=NULL*/)
{
  int ret = OB_SUCCESS;
  UNUSEDx(schema_service);
  table_name = OB_ALL_TABLE_HISTORY_TNAME;
  return ret;
}


bool ObSchemaUtils::is_virtual_generated_column(uint64_t flag)
{
  return flag & VIRTUAL_GENERATED_COLUMN_FLAG;
}

bool ObSchemaUtils::is_multivalue_generated_column(uint64_t flag)
{
  return flag & MULTIVALUE_INDEX_GENERATED_COLUMN_FLAG;
}

bool ObSchemaUtils::is_multivalue_generated_array_column(uint64_t flag)
{
  return flag & MULTIVALUE_INDEX_GENERATED_ARRAY_COLUMN_FLAG;
}

bool ObSchemaUtils::is_stored_generated_column(uint64_t flag)
{
  return flag & STORED_GENERATED_COLUMN_FLAG;
}

bool ObSchemaUtils::is_cte_generated_column(uint64_t flag)
{
  return flag & CTE_GENERATED_COLUMN_FLAG;
}

bool ObSchemaUtils::is_default_expr_v2_column(uint64_t flag)
{
  return flag & DEFAULT_EXPR_V2_COLUMN_FLAG;
}

/* vector index */
bool ObSchemaUtils::is_vec_index_column(const uint64_t flag)
{
  return is_vec_hnsw_vid_column(flag)
      || is_vec_hnsw_type_column(flag)
      || is_vec_hnsw_vector_column(flag)
      || is_vec_hnsw_scn_column(flag)
      || is_vec_hnsw_key_column(flag)
      || is_vec_hnsw_data_column(flag)
      || is_vec_ivf_center_id_column(flag)
      || is_vec_ivf_center_vector_column(flag)
      || is_vec_ivf_data_vector_column(flag)
      || is_vec_ivf_pq_center_id_column(flag)
      || is_vec_ivf_pq_center_ids_column(flag)
      || is_vec_ivf_meta_id_column(flag)
      || is_vec_ivf_meta_vector_column(flag)
      || is_vec_spiv_dim_column(flag)
      || is_vec_spiv_value_column(flag)
      || is_hybrid_vec_index_chunk_column(flag);
}

bool ObSchemaUtils::is_vec_spiv_dim_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_SPIV_DIM_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_spiv_value_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_SPIV_VALUE_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_spiv_vec_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_SPIV_VEC_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_ivf_center_id_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_IVF_CENTER_ID_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_ivf_center_vector_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_IVF_CENTER_VECTOR_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_ivf_data_vector_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_IVF_DATA_VECTOR_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_ivf_meta_id_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_IVF_META_ID_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_ivf_meta_vector_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_IVF_META_VECTOR_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_ivf_pq_center_id_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_IVF_PQ_CENTER_ID_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_ivf_pq_center_ids_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_IVF_PQ_CENTER_IDS_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_hnsw_vid_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_VID_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_hnsw_type_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_TYPE_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_hnsw_vector_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_VECTOR_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_hnsw_scn_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_SCN_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_hnsw_key_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_KEY_COLUMN_FLAG;
}

bool ObSchemaUtils::is_vec_hnsw_data_column(const uint64_t flag)
{
  return flag & GENERATED_VEC_DATA_COLUMN_FLAG;
}

bool ObSchemaUtils::is_hybrid_vec_index_chunk_column(const uint64_t flag)
{
  return flag & GENERATED_HYBRID_VEC_CHUNK_COLUMN_FLAG;
}

bool ObSchemaUtils::is_fulltext_column(const uint64_t flag)
{
  return is_doc_id_column(flag)
      || is_word_segment_column(flag)
      || is_word_count_column(flag)
      || is_doc_length_column(flag);
}

bool ObSchemaUtils::is_doc_id_column(const uint64_t flag)
{
  return flag & GENERATED_DOC_ID_COLUMN_FLAG;
}

bool ObSchemaUtils::is_word_segment_column(const uint64_t flag)
{
  return flag & GENERATED_FTS_WORD_SEGMENT_COLUMN_FLAG;
}

bool ObSchemaUtils::is_word_count_column(const uint64_t flag)
{
  return flag & GENERATED_FTS_WORD_COUNT_COLUMN_FLAG;
}

bool ObSchemaUtils::is_doc_length_column(const uint64_t flag)
{
  return flag & GENERATED_FTS_DOC_LENGTH_COLUMN_FLAG;
}

bool ObSchemaUtils::is_spatial_generated_column(uint64_t flag)
{
  return flag & SPATIAL_INDEX_GENERATED_COLUMN_FLAG;
}

int ObSchemaUtils::convert_sys_param_to_sysvar_schema(const ObSysParam &sysparam, ObSysVarSchema &sysvar_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sysvar_schema.set_name(ObString::make_string(sysparam.name_)))) {
    LOG_WARN("set sysvar schema name failed", K(ret));
  } else if (OB_FAIL(sysvar_schema.set_value(ObString::make_string(sysparam.value_)))) {
    LOG_WARN("set sysvar schema value failed", K(ret));
  } else if (OB_FAIL(sysvar_schema.set_min_val(ObString::make_string(sysparam.min_val_)))) {
    LOG_WARN("set sysvar schema min val failed", K(ret));
  } else if (OB_FAIL(sysvar_schema.set_max_val(ObString::make_string(sysparam.max_val_)))) {
    LOG_WARN("set sysvar schema max val failed", K(ret));
  } else if (OB_FAIL(sysvar_schema.set_info(ObString::make_string(sysparam.info_)))) {
    LOG_WARN("set sysvar schema info failed", K(ret));
  } else {
    sysvar_schema.set_flags(sysparam.flags_);
    
    sysvar_schema.set_data_type(static_cast<ObObjType>(sysparam.data_type_));
  }
  return ret;
}

bool ObSchemaUtils::is_support_parallel_drop(const ObTableType table_type)
{
  // TODO(ziqian.zzq): support more table type for parallel drop
  return USER_TABLE == table_type
         || TMP_TABLE == table_type;
}

int ObSchemaUtils::get_runtime_int_variable(ObSysVarClassType var_id,
                                           int64_t &v)
{
  int ret = OB_SUCCESS;
  schema::ObSchemaGetterGuard schema_guard;
  ObObj value;
  if (OB_FAIL(get_runtime_variable(schema_guard, var_id, value))) {
    LOG_WARN("fail to get runtime variable", K(value), K(var_id), K(ret));
  } else if (OB_FAIL(value.get_int(v))) {
    LOG_WARN("get int from value failed", K(ret), K(value));
  }
  return ret;
}

int ObSchemaUtils::get_runtime_varchar_variable(ObSysVarClassType var_id,
                                               ObIAllocator &allocator,
                                               ObString &v)
{
  int ret = OB_SUCCESS;
  schema::ObSchemaGetterGuard schema_guard;
  ObObj value;
  ObString tmp;
  if (OB_FAIL(get_runtime_variable(schema_guard, var_id, value))) {
    LOG_WARN("fail to get runtime variable", K(value), K(var_id), K(ret));
  } else if (OB_FAIL(value.get_varchar(tmp))) {
    LOG_WARN("get varchar from value failed", K(ret), K(value));
  } else if (OB_FAIL(ob_write_string(allocator, tmp, v))) {
    // must be deep copy, otherwise very low probability v will reference illegal memory
    LOG_WARN("fail deep copy string", K(ret));
  }
  return ret;
}

int ObSchemaUtils::get_runtime_variable(schema::ObSchemaGetterGuard &schema_guard,
                                       ObSysVarClassType var_id,
                                       ObObj &value)
{
  int ret = OB_SUCCESS;
  const schema::ObSysVarSchema *var_schema = NULL;
  share::schema::ObMultiVersionSchemaService &schema_service =
      share::schema::ObMultiVersionSchemaService::get_instance();
  if (OB_FAIL(schema_service.get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_system_variable(
              var_id, var_schema))) {
    LOG_WARN("fail to get system variable", K(ret), K(var_id));
  } else if (OB_ISNULL(var_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("var_schema is null");
  } else if (OB_FAIL(var_schema->get_value(NULL, NULL, value))) {
    LOG_WARN("get value from var_schema failed", K(ret), K(*var_schema));
  }
  return ret;
}

int ObSchemaUtils::str_to_int(const ObString &str, int64_t &value)
{
  int ret = OB_SUCCESS;
  char buf[OB_MAX_BIT_LENGTH];
  value = OB_INVALID_ID;
  if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(str));
  } else {
    int n = snprintf(buf, OB_MAX_BIT_LENGTH, "%.*s", str.length(), str.ptr());
    if (n < 0 || n >= OB_MAX_BIT_LENGTH) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_WARN("id_buf is not long enough", K(ret), K(n), LITERAL_K(OB_MAX_BIT_LENGTH));
    } else {
      const int64_t base = 10;
      value = strtoll(buf, NULL, base);
    }
  }
  return ret;
}

int ObSchemaUtils::str_to_uint(const ObString &str, uint64_t &value)
{
  int ret = OB_SUCCESS;
  int64_t int_value = OB_INVALID_ID;
  if (OB_FAIL(str_to_int(str, int_value))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to translate str to int", K(str), K(ret));
  } else {
    value = static_cast<uint64_t>(int_value);
  }
  return ret;
}

int ObSchemaUtils::construct_runtime_space_full_table(
    ObTableSchema &table)
{
  int ret = OB_SUCCESS;
  // index
  const int64_t table_id = table.get_table_id();
  if (OB_FAIL(ObSysTableChecker::fill_sys_index_infos(table))) {
    LOG_WARN("fail to fill sys indexes", KR(ret), K(table_id));
  }
  // lob aux
  if (OB_SUCC(ret) && is_system_table(table_id)) {
    uint64_t lob_meta_table_id = 0;
    uint64_t lob_piece_table_id = 0;
    if (OB_ALL_CORE_TABLE_TID == table_id) {
      // do nothing
    } else if (!get_sys_table_lob_aux_table_id(table_id, lob_meta_table_id, lob_piece_table_id)) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("fail to get lob aux table id", KR(ret), K(table_id));
    } else if (lob_meta_table_id == 0 || lob_piece_table_id == 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get lob aux table id", KR(ret), K(table_id), K(lob_meta_table_id), K(lob_piece_table_id));
    } else {
      table.set_aux_lob_meta_tid(lob_meta_table_id);
      table.set_aux_lob_piece_tid(lob_piece_table_id);
    }
  }
  // column
  int64_t column_count = table.get_column_count();
  for (int64_t i = 0; OB_SUCC(ret) && i < column_count; ++i) {
    ObColumnSchemaV2 *column = NULL;
    if (NULL == (column = table.get_column_schema_by_idx(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("column is null", K(ret));
    } else {
      column->set_table_id(table.get_table_id());
    }
  }
  return ret;
}

int ObSchemaUtils::add_sys_table_lob_aux_table(
    uint64_t data_table_id,
    ObIArray<ObTableSchema> &table_schemas)
{
  int ret = OB_SUCCESS;
  if (is_system_table(data_table_id)) {
    HEAP_VARS_2((ObTableSchema, lob_meta_schema), (ObTableSchema, lob_piece_schema)) {
      if (OB_ALL_CORE_TABLE_TID == data_table_id) {
        // do nothing
      } else if (OB_FAIL(get_sys_table_lob_aux_schema(data_table_id, lob_meta_schema, lob_piece_schema))) {
        LOG_WARN("fail to get sys table lob aux schema", KR(ret), K(data_table_id));
      } else if (OB_FAIL(ObSchemaUtils::construct_runtime_space_full_table(
                  lob_meta_schema))) {
        LOG_WARN("fail to construct runtime space table", KR(ret));
      } else if (OB_FAIL(ObSchemaUtils::construct_runtime_space_full_table(
                  lob_piece_schema))) {
        LOG_WARN("fail to construct runtime space table", KR(ret));
      } else if (OB_FAIL(table_schemas.push_back(lob_meta_schema))) {
        LOG_WARN("fail to push back table schema", KR(ret), K(lob_meta_schema));
      } else if (OB_FAIL(table_schemas.push_back(lob_piece_schema))) {
        LOG_WARN("fail to push back table schema", KR(ret), K(lob_piece_schema));
      }
    }
  }
  return ret;
}

// construct inner table schemas in runtime space
int ObSchemaUtils::construct_inner_table_schemas(ObSArray<ObTableSchema> &tables,
    ObIAllocator &allocator,
    bool construct_all)
{
  int ret = OB_SUCCESS;
  const schema_create_func *creator_ptr_arrays[] = {
    all_core_table_schema_creator,
    core_table_schema_creators,
    sys_table_schema_creators,
    virtual_table_schema_creators,
    sys_view_schema_creators
  };
  int64_t capacity = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < ARRAYSIZEOF(creator_ptr_arrays); ++i) {
    for (const schema_create_func *creator_ptr = creator_ptr_arrays[i];
        OB_SUCC(ret) && OB_NOT_NULL(*creator_ptr); ++creator_ptr) {
      ++capacity;
    }
  }
  if (FAILEDx(tables.prepare_allocate_and_keep_count(capacity, &allocator))) {
    LOG_WARN("fail to prepare allocate table schemas", KR(ret), K(capacity));
  }
  HEAP_VARS_2((ObTableSchema, table_schema), (ObTableSchema, data_schema)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < ARRAYSIZEOF(creator_ptr_arrays); ++i) {
      for (const schema_create_func *creator_ptr = creator_ptr_arrays[i];
          OB_SUCC(ret) && OB_NOT_NULL(*creator_ptr); ++creator_ptr) {
        table_schema.reset();
        bool exist = false;
        if (OB_FAIL((*creator_ptr)(table_schema))) {
          LOG_WARN("fail to gen sys table schema", KR(ret));
        } else if (!construct_all
            && table_schema.get_table_id() == OB_ALL_CORE_TABLE_TID) {
          // server runtime's __all_core_table's schema is built separately in bootstrap
        } else if (OB_FAIL(ObSchemaUtils::construct_runtime_space_full_table(
                table_schema))) {
          LOG_WARN("fail to construct runtime space table", KR(ret));
        } else if (OB_FAIL(ObSysTableChecker::is_inner_table_exist(
                table_schema, exist))) {
          LOG_WARN("fail to check inner table exist",
              KR(ret), K(table_schema));
        } else if (!construct_all && !exist) {
          // skip
        } else if (OB_FAIL(tables.push_back(table_schema))) {
          LOG_WARN("fail to push back table schema", KR(ret), K(table_schema));
        } else if (OB_FAIL(ObSysTableChecker::append_sys_table_index_schemas(
                table_schema.get_table_id(), tables))) {
          LOG_WARN("fail to append sys table index schemas",
              KR(ret), "table_id", table_schema.get_table_id());
        }
        const int64_t data_table_id = table_schema.get_table_id();
        if (OB_SUCC(ret) && exist) {
          if (OB_FAIL(add_sys_table_lob_aux_table(data_table_id, tables))) {
            LOG_WARN("fail to add lob table to sys table", KR(ret), K(data_table_id));
          }
        } // end lob aux table
      }
    }
  }
  return ret;
}

// used for generating hard code schema version when add table in development
// for virtual table with index, we should make index schema version less than virtual table schema version
// otherwise, schema service cannot bind index schema to virtual table schema
// system table index is no need to do this because system table indexes are hard code
// see also:
// the algorithm:
// 1. For the input array, construct a map from table_id to table pointer.
// 2. Traverse the array in reverse order. For virtual table index, first insert the corresponding virtual table into the table_id array, then insert its own table_id; for other system tables, insert them directly into table_id array. Ensure that the virtual table appears in the array before its index. At this point, the schema_version in the table_id array that ranks higher is larger.
// 3. Traverse the table_id array in order, if a table does not have a valid schema_version, assign it a schema_version from largest to smallest.

int ObSchemaUtils::generate_hard_code_schema_version(ObIArray<ObTableSchema> &tables)
{
  int ret = OB_SUCCESS;
  hash::ObHashMap<uint64_t, ObTableSchema *> tid2table;
  if (OB_FAIL(tid2table.create(tables.count(), "Tid2Table"))) {
    LOG_WARN("failed to create tid2table", KR(ret), K(tables.count()));
  } else {
    int64_t current_schema_version = (HARD_CODE_SCHEMA_VERSION_BEGIN + tables.count()) * ObSchemaVersionGenerator::SCHEMA_VERSION_INC_STEP;
    ObArray<uint64_t> table_id_in_schema_version_order;
    FOREACH_CNT_X(table, tables, OB_SUCC(ret)) {
      if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("pointer is null", KR(ret), KP(table));
      } else if (OB_FAIL(tid2table.set_refactored(table->get_table_id(), table))) {
        LOG_WARN("failed to add tid to table", KR(ret), K(table));
      }
    }
    for (int64_t i = tables.count() - 1; i >= 0 && OB_SUCC(ret); i--) {
      ObTableSchema &table = tables.at(i);
      table.set_schema_version(OB_INVALID_VERSION);
      if (table.is_index_table() && is_virtual_table(table.get_data_table_id())) {
        if (OB_FAIL(table_id_in_schema_version_order.push_back(table.get_data_table_id()))) {
          LOG_WARN("failed to push data_table_id", KR(ret), K(table));
        }
      }
      if (FAILEDx(table_id_in_schema_version_order.push_back(table.get_table_id()))) {
        LOG_WARN("failed to push table_id", KR(ret), K(table));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < table_id_in_schema_version_order.count(); i++) {
      const uint64_t table_id = table_id_in_schema_version_order.at(i);
      ObTableSchema **table_ptr = nullptr;
      ObTableSchema *table = nullptr;
      if (OB_ISNULL(table_ptr = tid2table.get(table_id)) || OB_ISNULL(table = *table_ptr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("pointer is null", KR(ret), KP(table_ptr), K(table));
      } else if (table->get_schema_version() != OB_INVALID_VERSION) {
        // table schema version is set, ignore
      } else {
        table->set_schema_version(current_schema_version);
        for (ObTableSchema::const_column_iterator iter = table->column_begin();
            OB_SUCC(ret) && iter != table->column_end(); ++iter) {
          (*iter)->set_schema_version(current_schema_version);
          (*iter)->set_table_id(table->get_table_id());
        }
        current_schema_version -= ObSchemaVersionGenerator::SCHEMA_VERSION_INC_STEP;
      }
    }
    if (OB_SUCC(ret) && current_schema_version != HARD_CODE_SCHEMA_VERSION_BEGIN *
        ObSchemaVersionGenerator::SCHEMA_VERSION_INC_STEP) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema count not match", KR(ret), K(current_schema_version), K(HARD_CODE_SCHEMA_VERSION_BEGIN));
    }
  }
  return ret;
}

int64_t ObSchemaUtils::get_inner_table_core_schema_version(ObIArray<ObTableSchema> &tables)
{
  int64_t core_schema_version = 0;
  for (int64_t i = 0; i < tables.count(); i++) {
    ObTableSchema &table = tables.at(i);
    if (is_core_table(table.get_table_id()) && table.get_schema_version() > core_schema_version) {
      core_schema_version = table.get_schema_version();
    }
  }
  return core_schema_version;
}

int64_t ObSchemaUtils::get_inner_table_sys_schema_version(ObIArray<ObTableSchema> &tables)
{
  int64_t sys_schema_version = 0;
  for (int64_t i = 0; i < tables.count(); i++) {
    ObTableSchema &table = tables.at(i);
    if (is_sys_table(table.get_table_id()) && table.get_schema_version() > sys_schema_version) {
      sys_schema_version = table.get_schema_version();
    }
  }
  return sys_schema_version;
}

// ObSchemaUtils::wait_local_schema_visible moved definition to the upper-layer owner cpp(real upper-layer symbol user, declaration remains in this class header, transitional state)

int ObSchemaUtils::batch_get_latest_table_schemas(
    common::ObISQLClient &sql_client,
    common::ObIAllocator &allocator,
    const common::ObIArray<ObObjectID> &table_ids,
    common::ObIArray<ObSimpleTableSchemaV2 *> &table_schemas)
{
  int ret = OB_SUCCESS;
  const int64_t schema_version = INT64_MAX - 1; // get latest schema
  if (OB_FAIL(batch_get_table_schemas_by_version(
      sql_client,
      allocator,
      schema_version,
      table_ids,
      table_schemas))) {
    LOG_WARN("batch get table schemas by version failed",
        KR(ret), K(table_ids), K(schema_version));
  }
  return ret;
}

int ObSchemaUtils::batch_get_table_schemas_by_version(
    common::ObISQLClient &sql_client,
    common::ObIAllocator &allocator,
    const int64_t schema_version,
    const common::ObIArray<ObObjectID> &table_ids,
    common::ObIArray<ObSimpleTableSchemaV2 *> &table_schemas)
{
  int ret = OB_SUCCESS;
  table_schemas.reset();
  ObSchemaService *schema_service = NULL;
  ObArray<ObTableLatestSchemaVersion> table_schema_versions;
  ObArray<SchemaKey> need_refresh_table_schema_keys;
  ObArray<ObSimpleTableSchemaV2 *> table_schemas_from_inner_table;
  if (OB_UNLIKELY(table_ids.empty() || schema_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(table_ids), K(schema_version));
  } else if (OB_ISNULL(GCTX.schema_service_)
      || OB_ISNULL(schema_service = GCTX.schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("multiversion_schema_service or schema_service is null", KR(ret));
  } else if (OB_FAIL(schema_service->get_table_latest_schema_versions(
      sql_client,
      table_ids,
      table_schema_versions))) {
    LOG_WARN("get table latest schema versions failed", KR(ret), K(table_ids));
  } else if (OB_FAIL(batch_get_table_schemas_from_cache_(
      allocator,
      schema_version,
      table_schema_versions,
      need_refresh_table_schema_keys,
      table_schemas))) {
    LOG_WARN("batch get table schemas from cache failed", KR(ret), K(table_schema_versions));
  } else if (OB_FAIL(batch_get_table_schemas_from_inner_table_(
      sql_client,
      allocator,
      schema_version,
      need_refresh_table_schema_keys,
      table_schemas_from_inner_table))) {
    LOG_WARN("batch get table_schemas from inner table failed", KR(ret), K(need_refresh_table_schema_keys));
  } else if (OB_FAIL(common::append(table_schemas, table_schemas_from_inner_table))) {
    LOG_WARN("append failed", KR(ret), "table_schemas count", table_schemas.count(),
        "table_schemas_from_inner_table count", table_schemas_from_inner_table.count());
  } else if (table_ids.count() != table_schemas.count()) {
    LOG_INFO("get less table_schemas, some tables have been deleted",
        "table_ids count", table_ids.count(), "table_schemas count", table_schemas.count(),
        K(table_ids), K(table_schema_versions), K(need_refresh_table_schema_keys));
  }
  // check table schema ptr
  ARRAY_FOREACH(table_schemas, idx) {
    const ObSimpleTableSchemaV2 *table_schema = table_schemas.at(idx);
    if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table_schema can't be null", KR(ret), K(idx), K(table_ids), K(table_schemas));
    }
  }
  return ret;
}

int ObSchemaUtils::check_sys_table_exist_by_sql(
    common::ObISQLClient &sql_client,
    const ObObjectID &table_id,
    bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  if (OB_UNLIKELY(OB_INVALID_ID == table_id
      || !is_sys_table(table_id)
      || is_core_table(table_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(table_id));
  } else {
    SMART_VAR(ObISQLClient::ReadResult, result) {
      ObSqlString sql;
      common::sqlclient::ObMySQLResult *res = NULL;
      // in __all_table, runtime id was a primary key and it's value is 0
      if (OB_FAIL(sql.append_fmt(
          "SELECT count(*) = 1 AS exist FROM %s WHERE table_id = %lu",
          OB_ALL_TABLE_TNAME, table_id))) {
        LOG_WARN("fail to assign sql", KR(ret));
      } else if (OB_FAIL(sql_client.read(result, sql.ptr()))) {
        LOG_WARN("execute sql failed", KR(ret), K(sql));
      } else if (OB_ISNULL(res = result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get mysql result failed", KR(ret), K(sql));
      } else if (OB_FAIL(res->next())) {
        LOG_WARN("next failed", KR(ret), K(sql));
      } else if (OB_FAIL(res->get_bool("exist", exist))) {
        LOG_WARN("get bool value failed", KR(ret), K(sql));
      }
    }
  }
  return ret;
}

int ObSchemaUtils::get_latest_table_schema(
    common::ObISQLClient &sql_client,
    common::ObIAllocator &allocator,
    const ObObjectID &table_id,
    ObSimpleTableSchemaV2 *&table_schema)
{
  int ret = OB_SUCCESS;
  table_schema = NULL;
  ObSEArray<ObObjectID, 1> table_ids;
  ObSEArray<ObSimpleTableSchemaV2 *, 1> table_schemas;
  if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(table_id));
  } else if (OB_FAIL(table_ids.push_back(table_id))) {
    LOG_WARN("push back failed", KR(ret), K(table_id), K(table_ids));
  } else if (OB_FAIL(batch_get_latest_table_schemas(
      sql_client,
      allocator,
      table_ids,
      table_schemas))) {
    LOG_WARN("batch get latest table schema failed", KR(ret), K(table_id));
  } else if (table_schemas.empty()) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist when get latest table schema", KR(ret), K(table_id));
  } else if (OB_ISNULL(table_schemas.at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table schema can not be null", KR(ret), K(table_id), K(table_schemas));
  } else {
    table_schema = table_schemas.at(0);
  }
  return ret;
}

int ObSchemaUtils::batch_get_table_schemas_from_cache_(
    common::ObIAllocator &allocator,
    const int64_t specified_schema_version,
    const ObIArray<ObTableLatestSchemaVersion> &table_schema_versions,
    common::ObIArray<SchemaKey> &need_refresh_table_schema_keys,
    common::ObIArray<ObSimpleTableSchemaV2 *> &table_schemas)
{
  int ret = OB_SUCCESS;
  need_refresh_table_schema_keys.reset();
  table_schemas.reset();
  ObSchemaGetterGuard schema_guard;
  if (OB_UNLIKELY(specified_schema_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(specified_schema_version));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("multiversion_schema_service is null", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(
      schema_guard))) {
    LOG_WARN("get schema guard failed", KR(ret));
  } else {
    ARRAY_FOREACH(table_schema_versions, idx) {
      const ObSimpleTableSchemaV2 *cached_table_schema = NULL;
      ObSimpleTableSchemaV2 *new_table_schema = NULL;
      const ObTableLatestSchemaVersion &table_schema_version = table_schema_versions.at(idx);
      if (table_schema_version.is_deleted()) {
        LOG_INFO("table has been deleted", K(table_schema_version));
        // skip
      } else if (OB_FAIL(schema_guard.get_simple_table_schema(
          table_schema_version.get_table_id(),
          cached_table_schema))) {
        LOG_WARN("get simple table schema failed", KR(ret), K(table_schema_version));
      } else if (OB_ISNULL(cached_table_schema)
          || (cached_table_schema->get_schema_version() < table_schema_version.get_schema_version())
          || (cached_table_schema->get_schema_version() > specified_schema_version)) {
        // need fetch new table schema
        SchemaKey table_schema_key;
        
        table_schema_key.table_id_ = table_schema_version.get_table_id();
        if (OB_FAIL(need_refresh_table_schema_keys.push_back(table_schema_key))) {
          LOG_WARN("push back failed", KR(ret), K(table_schema_version));
        }
      } else if (OB_FAIL(alloc_schema(allocator, *cached_table_schema, new_table_schema))) {
        LOG_WARN("fail to alloc schema", KR(ret), KPC(cached_table_schema));
      } else if (OB_FAIL(table_schemas.push_back(new_table_schema))) {
        LOG_WARN("push back failed", KR(ret), KP(new_table_schema));
      }
    } // end ARRAY_FOREACH
  }
  return ret;
}

int ObSchemaUtils::batch_get_table_schemas_from_inner_table_(
    common::ObISQLClient &sql_client,
    common::ObIAllocator &allocator,
    const int64_t schema_version,
    common::ObArray<SchemaKey> &need_refresh_table_schema_keys,
    common::ObIArray<ObSimpleTableSchemaV2 *> &table_schemas)
{
  int ret = OB_SUCCESS;
  table_schemas.reset();
  ObSchemaService *schema_service = NULL;
  ObRefreshSchemaStatus schema_status;
  
  if (OB_UNLIKELY(schema_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(schema_version));
  } else if (OB_ISNULL(GCTX.schema_service_)
      || OB_ISNULL(schema_service = GCTX.schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("multiversion_schema_service or schema_service is null", KR(ret));
  } else if (need_refresh_table_schema_keys.empty()) {
    // skip
  } else if (OB_FAIL(schema_service->get_batch_tables(
      schema_status,
      sql_client,
      allocator,
      schema_version,
      need_refresh_table_schema_keys,
      table_schemas))) {
    LOG_WARN("get batch tables failed", KR(ret),
        K(schema_status), K(schema_version), K(need_refresh_table_schema_keys));
  }
  return ret;
}

const char* DDLType[]
{
  "TRUNCATE_TABLE",
  "SET_COMMENT",
  "CREATE_INDEX",
  "CREATE_VIEW",
  "DROP_TABLE"
};

int ObParallelDDLControlMode::string_to_ddl_type(const ObString &ddl_string, ObParallelDDLType &ddl_type)
{
  int ret = OB_SUCCESS;
  ddl_type = MAX_TYPE;
  STATIC_ASSERT((ARRAYSIZEOF(DDLType)) == MAX_TYPE, "size count not match");
  bool find = false;
  for (uint64_t i = 0; !find && i < ARRAYSIZEOF(DDLType); i++) {
    if (ddl_string.case_compare(DDLType[i]) == 0) {
      find = true;
      ddl_type = static_cast<ObParallelDDLType>(i);
    }
  }
  if (OB_UNLIKELY(!find)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "unknown ddl_type", KR(ret), K(ddl_string));
  }
  return ret;
}

int ObParallelDDLControlMode::set_value(const ObConfigModeItem &mode_item)
{
  int ret = OB_SUCCESS;
  const uint8_t* values = mode_item.get_value();
  if (OB_ISNULL(values)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "mode item's value_ is null ptr", KR(ret));
  } else {
    STATIC_ASSERT(((sizeof(value_)/sizeof(uint8_t) <= ObConfigModeItem::MAX_MODE_BYTES)),
                  "value_ size overflow");
    STATIC_ASSERT( (MAX_TYPE * 2) <= (sizeof(value_) * 8), "type size overflow");
    value_ = 0;
    for (uint64_t i = 0; i < 8; ++i) {
      value_ = (value_ | static_cast<uint64_t>(values[i]) << (8 * i));
    }
  }
  return ret;
}

int ObParallelDDLControlMode::set_parallel_ddl_mode(const ObParallelDDLType type, const uint8_t mode)
{
  int ret = OB_SUCCESS;
  if ((TRUNCATE_TABLE <= type) && (type < MAX_TYPE)) {
    uint64_t shift = static_cast<uint64_t>(type);
    if (!check_mode_valid_(mode)) {
      ret = OB_INVALID_ARGUMENT;
      OB_LOG(WARN, "mode invalid", KR(ret), K(mode));
    } else {
      uint64_t mask = MASK << (shift * MASK_SIZE);
      value_ = (value_ & ~mask) | (static_cast<uint64_t>(mode) << (shift * MASK_SIZE));
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "type invalid", KR(ret), K(type));
  }
  return ret;
}

int ObParallelDDLControlMode::is_parallel_ddl(const ObParallelDDLType type, bool &is_parallel)
{
  int ret = OB_SUCCESS;
  is_parallel = true;
  if ((TRUNCATE_TABLE <= type) && (type < MAX_TYPE)) {
    uint64_t shift = static_cast<uint64_t>(type);
    uint8_t value = static_cast<uint8_t>((value_ >> (shift * MASK_SIZE)) & MASK);
    if (value == ObParallelDDLControlParser::MODE_OFF) {
      is_parallel = false;
    } else if (value == ObParallelDDLControlParser::MODE_ON) {
      is_parallel = true;
    } else if (value == ObParallelDDLControlParser::MODE_DEFAULT) {
      is_parallel = true;
    } else {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(WARN, "invalid value unexpected", KR(ret), K(value));
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "type invalid", KR(ret), K(type));
  }
  return ret;
}

// is_parallel_ddl_enable: restored back to share(omt::ObRuntimeConfigGuard/RUNTIME_CONF actually lives in share/config, originally misclassified as observer)
int ObParallelDDLControlMode::is_parallel_ddl_enable(const ObParallelDDLType ddl_type, bool &is_parallel)
{
  int ret = OB_SUCCESS;
  is_parallel = true;
  ObParallelDDLControlMode cfg;
  if (OB_FAIL(GCONF._parallel_ddl_control.init_mode(cfg))) {
    LOG_WARN("init mode failed", KR(ret));
  } else if (OB_FAIL(cfg.is_parallel_ddl(ddl_type, is_parallel))) {
    LOG_WARN("fail to check is parallel ddl", KR(ret), K(ddl_type));
  }
  return ret;
}

} // end schema
} // end share
} // end oceanbase
