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

#define USING_LOG_PREFIX PL

#include "ob_dbms_vector_mysql.h"
#include "src/pl/ob_pl.h"
#include "sql/engine/cmd/ob_vector_refresh_index_executor.h"
#include "lib/vector/ob_vsag_adaptor.h"
#include "share/ob_lob_access_utils.h"
#include <vector>
#include <cstdlib>

namespace oceanbase
{
namespace pl
{
using namespace common;
using namespace sql;

/*
PROCEDURE refresh_index(
  IN       IDX_NAME            VARCHAR(65535),               ---- Index name
  IN       TABLE_NAME          VARCHAR(65535),               ---- Table name
  IN       IDX_VECTOR_COL      VARCHAR(65535) DEFAULT NULL,  ---- Vector column name
  IN       REFRESH_THRESHOLD   INT DEFAULT 10000,            ---- Trigger incremental refresh when the number of records in table 3 reaches the threshold
  IN       REFRESH_TYPE        VARCHAR(65535) DEFAULT NULL   ---- Reserved: Current default behavior is incremental refresh: FAST
);
*/
int ObDBMSVectorMySql::refresh_index(ObPLExecCtx &ctx, ParamStore &params, ObObj &result)
{
  UNUSED(result);
  int ret = OB_SUCCESS;
  CK(OB_LIKELY(5 == params.count()));
  if (!params.at(0).is_varchar()
      || !params.at(1).is_varchar()
      || (!params.at(2).is_null() && !params.at(2).is_varchar())
      || !(!params.at(3).is_null() && params.at(3).is_int32())
      || (!params.at(4).is_null() && !params.at(4).is_varchar())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument for refresh index", KR(ret));
  }
  if (OB_SUCC(ret)) {
      ObVectorRefreshIndexArg refresh_arg;
      ObVectorRefreshIndexExecutor refresh_executor;
      refresh_arg.idx_name_ = params.at(0).get_varchar();
      refresh_arg.table_name_ = params.at(1).get_varchar();
      params.at(2).is_varchar() ? refresh_arg.idx_vector_col_ = params.at(2).get_varchar() : NULL;
      refresh_arg.refresh_threshold_ = params.at(3).get_int();
      params.at(4).is_varchar() ? refresh_arg.refresh_type_ = params.at(4).get_varchar() : NULL;
      if (OB_FAIL(refresh_executor.execute_refresh(ctx.exec_ctx_, ctx.allocator_, refresh_arg))) {
      }
  }
  return ret;
}

/*
PROCEDURE rebuild_index (
    IN      IDX_NAME                VARCHAR(65535),                      ---- Index name
    IN      TABLE_NAME              VARCHAR(65535),                      ---- Table name
    IN      IDX_VECTOR_COL          VARCHAR(65535) DEFAULT NULL,         ---- Vector column name
    IN      DELTA_RATE_THRESHOLD    FLOAT DEFAULT 0.2,                   ---- Trigger rebuild when (number of records in table 3 + number of records in table 4) / base table record count reaches the threshold
    IN      IDX_ORGANIZATION        VARCHAR(65535) DEFAULT NULL,         ---- Index type, modification of index type is not allowed in this release
    IN      IDX_DISTANCE_METRICS    VARCHAR(65535) DEFAULT 'EUCLIDEAN',  ---- Distance type, modification is not allowed in this release
    IN      IDX_PARAMETERS          LONGTEXT DEFAULT NULL,               ---- Index parameters, modification is not allowed in this release
    IN      IDX_PARALLEL_CREATION   INT DEFAULT 1                        ---- Parallelism degree for parallel index creation, reserved for future use, syntax support only
);
*/
int ObDBMSVectorMySql::rebuild_index(ObPLExecCtx &ctx, ParamStore &params, ObObj &result)
{
  UNUSED(result);
  int ret = OB_SUCCESS;
  CK(OB_LIKELY(8 == params.count()));
  if (!params.at(0).is_varchar()
      || !params.at(1).is_varchar()
      || (!params.at(2).is_null() && !params.at(2).is_varchar())
      || !(!params.at(3).is_null() && params.at(3).is_float())
      || (!params.at(4).is_null() && !params.at(4).is_varchar())
      || !params.at(5).is_varchar()
      || (!params.at(6).is_null() && !params.at(6).is_text())
      || !(!params.at(7).is_null() && params.at(7).is_int32())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument for rebuild index", KR(ret));
  }
  if (OB_SUCC(ret)) {
      ObVectorRebuildIndexArg rebuild_arg;
      ObVectorRefreshIndexExecutor rebuild_executor;
      rebuild_arg.idx_name_ = params.at(0).get_varchar();
      rebuild_arg.table_name_ = params.at(1).get_varchar();
      params.at(2).is_varchar() ? rebuild_arg.idx_vector_col_ = params.at(2).get_varchar() : NULL;
      rebuild_arg.delta_rate_threshold_ = params.at(3).get_float();
      params.at(4).is_varchar() ? rebuild_arg.idx_organization_ = params.at(4).get_varchar() : NULL;
      rebuild_arg.idx_distance_metrics_ = params.at(5).get_varchar();
      rebuild_arg.idx_parallel_creation_ = params.at(7).get_int();

      rebuild_arg.idx_parameters_ = NULL;
      if (params.at(6).is_text() && OB_FAIL(params.at(6).get_string(rebuild_arg.idx_parameters_))) {
          LOG_WARN("fail to get string", K(ret));
      } else if (OB_FAIL(rebuild_executor.execute_rebuild(ctx.exec_ctx_, ctx.allocator_, rebuild_arg))) {
      }
  }
  return ret;
}

int ObDBMSVectorMySql::refresh_index_inner(ObPLExecCtx &ctx, ParamStore &params, ObObj &result)
{
  UNUSED(result);
  int ret = OB_SUCCESS;
  CK(OB_LIKELY(3 == params.count()));
  CK(OB_LIKELY(params.at(0).is_int()),
      OB_LIKELY(params.at(1).is_int32()),
      OB_LIKELY(params.at(2).is_null() || params.at(2).is_varchar()));
  if (OB_SUCC(ret)) {
    ObVectorRefreshIndexInnerArg refresh_arg;
    ObVectorRefreshIndexExecutor refresh_executor;
    refresh_arg.idx_table_id_ = params.at(0).get_int();
    refresh_arg.refresh_threshold_ = params.at(1).get_int();
    params.at(2).is_varchar() ? refresh_arg.refresh_type_ = params.at(2).get_varchar() : NULL;
    if (OB_FAIL(refresh_executor.execute_refresh_inner(ctx.exec_ctx_, ctx.allocator_, refresh_arg))) {
    }
  }
  return ret;
}

int ObDBMSVectorMySql::rebuild_index_inner(ObPLExecCtx &ctx, ParamStore &params, ObObj &result)
{
  UNUSED(result);
  int ret = OB_SUCCESS;
  CK(OB_LIKELY(6 == params.count()));
  CK(OB_LIKELY(params.at(0).is_int()),
      OB_LIKELY(params.at(1).is_float()),
      OB_LIKELY(params.at(2).is_null() || params.at(2).is_varchar()),
      OB_LIKELY(params.at(3).is_varchar()),
      OB_LIKELY(params.at(4).is_null() || params.at(4).is_text()),
      OB_LIKELY(params.at(5).is_int32()));
  if (OB_SUCC(ret)) {
    ObVectorRebuildIndexInnerArg rebuild_arg;
    ObVectorRefreshIndexExecutor rebuild_executor;
    rebuild_arg.idx_table_id_ = params.at(0).get_int();
    rebuild_arg.delta_rate_threshold_ = params.at(1).get_float();
    params.at(2).is_varchar() ? rebuild_arg.idx_organization_ = params.at(2).get_varchar() : NULL;
    rebuild_arg.idx_distance_metrics_ = params.at(3).get_varchar();
    rebuild_arg.idx_parallel_creation_ = params.at(5).get_int();

    rebuild_arg.idx_parameters_ = NULL;
    if (params.at(4).is_text() && OB_FAIL(params.at(4).get_string(rebuild_arg.idx_parameters_))) {
        LOG_WARN("fail to get string", K(ret));
    } else if (OB_FAIL(rebuild_executor.execute_rebuild_inner(ctx.exec_ctx_, ctx.allocator_, rebuild_arg))) {
    }
  }
  return ret;
}

/*
FUNCTION index_vector_memory_advisor (
    IN     idx_type           VARCHAR(65535),
    IN     num_vectors        BIGINT UNSIGNED,
    IN     dim_count          INT UNSIGNED,
    IN     dim_type           VARCHAR(65535) DEFAULT 'FLOAT32',
    IN     idx_parameters     LONGTEXT DEFAULT NULL,
    IN     max_tablet_vectors BIGINT UNSIGNED DEFAULT 0)
RETURN VARCHAR(65535);
*/
int ObDBMSVectorMySql::index_vector_memory_advisor(ObPLExecCtx &ctx, ParamStore &params, ObObj &result)
{
  int ret = OB_SUCCESS;
  CK(OB_LIKELY(6 == params.count()));
  if (OB_FAIL(ret)) {
  } else if (!params.at(0).is_varchar()
             || !params.at(1).is_uint64()
             || !params.at(2).is_uint32()
             || !params.at(3).is_varchar()
             || (!params.at(4).is_text() && !params.at(4).is_null())
             || !params.at(5).is_uint64()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    ObIAllocator *allocator = &ctx.exec_ctx_->get_allocator();
    ObString idx_type_str = params.at(0).get_varchar();
    uint64_t num_vectors = params.at(1).get_uint64();
    uint64_t max_tablet_vectors = params.at(5).get_uint64();
    if (max_tablet_vectors == 0) {
      max_tablet_vectors = num_vectors;
    }
    uint32_t dim_count = params.at(2).get_uint32();
    ObString dim_type_str = params.at(3).get_varchar();
    ObString idx_param_str;
    share::ObVectorIndexParam index_param;

    if (max_tablet_vectors > num_vectors) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("invalid max_tablet_vectors", KR(ret), K(max_tablet_vectors), K(num_vectors));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "max_tablet_vectors large than num_vectors");
    } else if (dim_type_str.case_compare("FLOAT32") != 0) { // for future use
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not support vector index dim type", K(ret), K(dim_type_str));
    } else if (params.at(4).is_text() && OB_FAIL(params.at(4).get_string(idx_param_str))) {
      LOG_WARN("failed to get index param string", K(ret));
    } else if (OB_FAIL(parse_idx_param(idx_type_str, idx_param_str, dim_count, index_param))) {
    } else if (OB_ISNULL(allocator)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("allocator is null", K(ret));
    } else {
      ObStringBuffer res_buf(allocator);
      if (OB_FAIL(get_estimate_memory_str(index_param, num_vectors, max_tablet_vectors, res_buf))) {
      } else {
        result.set_varchar(res_buf.ptr(), res_buf.length());
        result.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      }
    }
  }
  return ret;
}

/*
FUNCTION index_vector_memory_estimate (
    IN     table_name        VARCHAR(65535),
    IN     column_name       VARCHAR(65535),
    IN     idx_type          VARCHAR(65535),
    IN     idx_parameters    LONGTEXT DEFAULT NULL)
RETURN VARCHAR(65535);
*/
int ObDBMSVectorMySql::index_vector_memory_estimate(ObPLExecCtx &ctx, ParamStore &params, ObObj &result)
{
  int ret = OB_SUCCESS;
  CK(OB_LIKELY(4 == params.count()));
  if (OB_FAIL(ret)) {
  } else if (!params.at(0).is_varchar()
             || !params.at(1).is_varchar()
             || !params.at(2).is_varchar()
             || (!params.at(3).is_text() && !params.at(4).is_null())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    ObIAllocator *allocator = &ctx.exec_ctx_->get_allocator();
    sql::ObSQLSessionInfo *session_info;
    sql::ObExecContext *exec_ctx = NULL;
    share::schema::ObSchemaGetterGuard *schema_guard = NULL;
    ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
    ObCollationType cs_type = CS_TYPE_INVALID;

    ObString param_table_name = params.at(0).get_varchar();
    ObString column_name = params.at(1).get_varchar();
    ObString idx_type_str = params.at(2).get_varchar();
    ObString idx_param_str;

    ObString database_name, table_name;
    uint64_t table_id = OB_INVALID_ID;
    const ObColumnSchemaV2 *col_schema = nullptr;

    int64_t dim_count = 0;
    uint64_t num_vectors = 0;
    uint64_t tablet_max_num_vectors = 0;
    share::ObVectorIndexParam index_param;

    // resolve table name and column name, 
    if (OB_ISNULL(allocator)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("allocator is null", K(ret));
    } else if (OB_ISNULL(exec_ctx = ctx.exec_ctx_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("exec context is null", KR(ret));
    } else if (OB_ISNULL(session_info = exec_ctx->get_my_session())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session info is null", KR(ret));
    } else if (OB_ISNULL(schema_guard = exec_ctx->get_virtual_table_ctx().schema_guard_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema guard is null", KR(ret));
    } else if (OB_FAIL(session_info->get_name_case_mode(case_mode))) {
    } else if (OB_FAIL(session_info->get_collation_connection(cs_type))) {
    } else if (OB_FAIL(ObVectorRefreshIndexExecutor::resolve_table_name(
                  cs_type, case_mode, param_table_name,
                  database_name, table_name))) {
    } else if (database_name.empty() && FALSE_IT(database_name = session_info->get_database_name())) {
    } else if (OB_UNLIKELY(database_name.empty())) {
      ret = OB_ERR_NO_DB_SELECTED;
      LOG_WARN("No database selected", KR(ret));
    } else if (OB_FAIL(schema_guard->get_table_id(
                  database_name,
                  table_name,
                  false, /*is_index*/
                  ObSchemaGetterGuard::ALL_NON_HIDDEN_TYPES,
                  table_id))) {
    } else if (table_id == OB_INVALID_ID) {
      ret = OB_TABLE_NOT_EXIST;
      ObCStringHelper helper;
      LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(database_name), helper.convert(table_name));
    } else if (OB_FAIL(schema_guard->get_column_schema(
                   table_id,
                   column_name,
                   col_schema))) {
    } else if (OB_ISNULL(col_schema)) {
      ret = OB_ERR_COLUMN_NOT_FOUND;
      LOG_WARN("column not found", K(ret));
    } else if (OB_FAIL(ObVectorIndexUtil::get_vector_dim_from_extend_type_info(col_schema->get_extended_type_info(), dim_count))) {
    } else {
      // get row count of the target table
      const int64_t sum_pos = 0;
      const int64_t max_pos = 1;
      ObObj sum_result_obj;
      ObObj max_result_obj;
      
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        ObSqlString query_string;
        sqlclient::ObMySQLResult *result = NULL;
        if (OB_FAIL(query_string.assign_fmt("SELECT cast(sum(table_rows) as unsigned) as sum, max(table_rows) as max from information_schema.PARTITIONS WHERE table_schema='%.*s' and table_name='%.*s'",
                database_name.length(), database_name.ptr(), table_name.length(), table_name.ptr()))) {
        } else if (OB_FAIL(GCTX.sql_proxy_->read(res, query_string.ptr()))) {
        } else if (OB_ISNULL(result = res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("fail to get sql result", K(ret), K(query_string));
        } else if (OB_FAIL(result->next())) {
        } else if (OB_FAIL(result->get_obj(sum_pos, sum_result_obj))) {
        } else if (OB_FAIL(result->get_obj(max_pos, max_result_obj))) {
        } else if ((!sum_result_obj.is_null() && OB_UNLIKELY(!sum_result_obj.is_integer_type())) ||
                   (!max_result_obj.is_null() && OB_UNLIKELY(!max_result_obj.is_integer_type()))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected obj type", K(ret), K(sum_result_obj.get_type()), K(max_result_obj.get_type()));
        } else if (!sum_result_obj.is_null() && OB_FALSE_IT(num_vectors = sum_result_obj.get_int())) {
        } else if (!max_result_obj.is_null() && OB_FALSE_IT(tablet_max_num_vectors = max_result_obj.get_int())) {
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (params.at(3).is_text() && OB_FAIL(params.at(3).get_string(idx_param_str))) {
      LOG_WARN("fail to get index param string", K(ret));
    } else if (idx_param_str.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid params", K(ret), K(idx_param_str));
    } else if (OB_FAIL(parse_idx_param(idx_type_str, idx_param_str, dim_count, index_param))) {
    } else if (OB_ISNULL(allocator)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("allocator is null", K(ret));
    } else {
      ObStringBuffer res_buf(allocator);
      if (OB_FAIL(get_estimate_memory_str(index_param, num_vectors, tablet_max_num_vectors, res_buf))) {
      } else {
        result.set_varchar(res_buf.ptr(), res_buf.length());
        result.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      }
    }
  }

  return ret;
}

int ObDBMSVectorMySql::parse_idx_param(const ObString &idx_type_str,
                                       const ObString &idx_param_str,
                                       uint32_t dim_count,
                                       share::ObVectorIndexParam &index_param)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc;
  ObVectorIndexType idx_type = VIT_MAX;
  ObStringBuffer param_str_buf(&tmp_alloc);
  ObString param_str;

  // parse idx_type
  if (idx_type_str.case_compare("HNSW") == 0
      || idx_type_str.case_compare("HNSW_SQ") == 0
      || idx_type_str.case_compare("HNSW_BQ") == 0
      || idx_type_str.case_compare("SINDI") == 0) {
    idx_type = ObVectorIndexType::VIT_HNSW_INDEX;
  } else if (idx_type_str.case_compare("IVF_FLAT") == 0
             || idx_type_str.case_compare("IVF_SQ8") == 0
             || idx_type_str.case_compare("IVF_PQ") == 0) {
    idx_type = ObVectorIndexType::VIT_IVF_INDEX;
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support vector index type", K(ret), K(idx_type_str));
  }

  // parse idx_param
  if (OB_FAIL(ret)) {
  } else if (idx_param_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid params", K(ret), K(idx_param_str));
  } else if (OB_FAIL(param_str_buf.append(idx_param_str))) {
  } else if (OB_FAIL(param_str_buf.append(",TYPE="))) {
  } else if (OB_FAIL(param_str_buf.append(idx_type_str))) {
  } else if (OB_FAIL(param_str_buf.get_result_string(param_str))) {
  } else if (OB_FAIL(ob_simple_low_to_up(tmp_alloc, param_str, param_str))) {
  } else if (OB_FAIL(ObVectorIndexUtil::parser_params_from_string(param_str, idx_type, index_param))) {
  } else if (index_param.dist_algorithm_ == VIDA_MAX) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unexpected setting of vector index param, distance has not been set", 
      K(ret), K(index_param.dist_algorithm_));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "the vector index params of distance not set is");
  } else {
    index_param.dim_ = dim_count;
  }

  return ret;
}

int ObDBMSVectorMySql::get_estimate_memory_str(share::ObVectorIndexParam index_param,
                                               uint64_t num_vectors,
                                               uint64_t tablet_max_num_vectors,
                                               ObStringBuffer &res_buf)
{
  int ret = OB_SUCCESS;
  const static double VEC_MEMORY_HOLD_FACTOR = 1.2;
  switch(index_param.type_) {
    case ObVectorIndexAlgorithmType::VIAT_HNSW:
    case ObVectorIndexAlgorithmType::VIAT_HNSW_SQ:
    case ObVectorIndexAlgorithmType::VIAT_HGRAPH: {
      uint64_t estimate_mem = 0;
      uint64_t max_tablet_estimate_mem = 0;
      if (OB_FAIL(ObVectorIndexUtil::estimate_hnsw_memory(num_vectors, index_param, estimate_mem))) {
      } else if (OB_FAIL(ObVectorIndexUtil::estimate_hnsw_memory(tablet_max_num_vectors, index_param, max_tablet_estimate_mem))) {
      } else if (OB_FALSE_IT(estimate_mem = ceil((estimate_mem + max_tablet_estimate_mem) * VEC_MEMORY_HOLD_FACTOR))) { // multiple 1.2
      } else if (OB_FAIL(res_buf.append(ObString("Suggested minimum vector memory is "), estimate_mem))) {
      } else if (OB_FAIL(print_mem_size(estimate_mem, res_buf))) {
      }
      break;
    }
    case ObVectorIndexAlgorithmType::VIAT_HNSW_BQ: {
      uint64_t estimate_mem = 0;
      uint64_t suggested_mem = 0;
      if (OB_FAIL(ObVectorIndexUtil::estimate_hnsw_memory(num_vectors, index_param, estimate_mem, false/*+is_build*/))) {
      } else if (OB_FALSE_IT(estimate_mem = ceil(estimate_mem * VEC_MEMORY_HOLD_FACTOR))) { // multiple 1.2
      } else if (OB_FAIL(ObVectorIndexUtil::estimate_hnsw_memory(tablet_max_num_vectors, index_param, suggested_mem, true/*+is_build*/))) {
      } else {
        suggested_mem = estimate_mem + suggested_mem * VEC_MEMORY_HOLD_FACTOR;
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(res_buf.append(ObString("Suggested minimum vector memory is "), 0))) {
      } else if (OB_FAIL(print_mem_size(suggested_mem, res_buf))) {
      } else if (OB_FAIL(res_buf.append(ObString(", memory consumption when providing search service is "), 0))) {
      } else if (OB_FAIL(print_mem_size(estimate_mem, res_buf))) {
      }
      break;
    }
    case ObVectorIndexAlgorithmType::VIAT_IVF_FLAT:
    case ObVectorIndexAlgorithmType::VIAT_IVF_SQ8:
    case ObVectorIndexAlgorithmType::VIAT_IVF_PQ: {
      uint64_t suggested_mem = 0;
      uint64_t buff_mem = 0;
      uint64_t construct_mem = 0;
      if (OB_FAIL(ObVectorIndexUtil::estimate_ivf_memory(num_vectors, index_param, construct_mem, buff_mem))) {
      } else if (OB_FALSE_IT(suggested_mem = construct_mem + buff_mem)) {
      } else if (OB_FAIL(res_buf.append(ObString("Suggested minimum vector memory is "), 0))) {
      } else if (OB_FAIL(print_mem_size(suggested_mem, res_buf))) {
      } else if (OB_FAIL(res_buf.append(ObString(", memory consumption when providing search service is "), 0))) {
      } else if (OB_FAIL(print_mem_size(buff_mem, res_buf))) {
      }
      break;
    }
    case ObVectorIndexAlgorithmType::VIAT_IPIVF: {
      uint64_t estimate_mem = 0;
      uint64_t max_tablet_estimate_mem = 0;
      if (OB_FAIL(ObVectorIndexUtil::estimate_sparse_memory(num_vectors, index_param, estimate_mem))) {
      } else if (OB_FAIL(ObVectorIndexUtil::estimate_sparse_memory(
                     tablet_max_num_vectors, index_param, max_tablet_estimate_mem))) {
      } else if (OB_FALSE_IT(estimate_mem = ceil(
                                 (estimate_mem + max_tablet_estimate_mem) * VEC_MEMORY_HOLD_FACTOR))) {  // multiple 1.2
      } else if (OB_FAIL(res_buf.append(ObString("Suggested minimum vector memory is "), estimate_mem))) {
      } else if (OB_FAIL(print_mem_size(estimate_mem, res_buf))) {
      }
      break;
    }
    case ObVectorIndexAlgorithmType::VIAT_SPIV: 
    {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "esitamte sparse vector memory is");
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid ivf algorithm type", K(ret), K(index_param));
    }
  }
  return ret;
}

int ObDBMSVectorMySql::print_mem_size(uint64_t mem_size, ObStringBuffer &res_buf)
{
  int ret = OB_SUCCESS;
  if (mem_size < 1024) {
    ObFastFormatInt size_str(mem_size);
    if (OB_FAIL(res_buf.append(size_str.ptr(), size_str.length(), 0))) {
    } else if (OB_FAIL(res_buf.append(ObString(" Bytes"), 0))) {
    }
  } else {
    const char* units[] = {"KB", "MB", "GB"};
    char mem_size_str[128] = "";
    int unit_index = 0;
    float float_mem_size = mem_size / 1024.0;
    while (float_mem_size >= 1024 && unit_index < 2) {
      float_mem_size /= 1024;
      unit_index++;
    }
    int res_len = snprintf(mem_size_str, 128, "%.1f %s", float_mem_size, units[unit_index]);
    if (OB_FAIL(res_buf.append(mem_size_str,res_len, 0))) {
    }
  }
  return ret;
}


// ---- [hipVS/cuVS] dbms_vector.batch_knn: SQL-callable BATCHED ANN ----
// Reads probe + index vectors from SQL, builds one CAGRA over the index table,
// runs ONE GPU batch search (obvsag::cuvs_batch_knn), writes neighbors to out_table.
// Convention: index_table/probe_table have 2 cols (col0=id int, col1=vector);
// out_table pre-created as (probe_id bigint, neighbor_id bigint, distance float, rk int).
static int batch_knn_read_vectors(const common::ObString &db,
                                  const common::ObString &tbl,
                                  std::vector<float> &vecs,
                                  std::vector<int64_t> &ids, int &dim)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator tmp_alloc;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    common::ObSqlString sql;
    sqlclient::ObMySQLResult *result = NULL;
    if (OB_ISNULL(GCTX.sql_proxy_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(sql.assign_fmt("SELECT * FROM `%.*s`.`%.*s` ORDER BY 1",
                   db.length(), db.ptr(), tbl.length(), tbl.ptr()))) {
    } else if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr()))) {
      LOG_WARN("batch_knn: read table failed", K(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      while (OB_SUCC(ret) && OB_SUCC(result->next())) {
        ObObj id_obj;
        ObObj vec_obj;
        if (OB_FAIL(result->get_obj(static_cast<int64_t>(0), id_obj))) {
        } else if (OB_FAIL(result->get_obj(static_cast<int64_t>(1), vec_obj))) {
        } else if (vec_obj.is_null()) {
          ret = OB_ERR_NULL_VALUE;
        } else {
          int64_t id = id_obj.is_int() ? id_obj.get_int() : static_cast<int64_t>(ids.size());
          common::ObString vs = vec_obj.get_string();
          if (vec_obj.has_lob_header()
              && OB_FAIL(common::lob_helper::read_real_string_data(&tmp_alloc, vec_obj, vs, NULL))) {
            LOG_WARN("batch_knn: read lob failed", K(ret));
          } else {
            const char *p = vs.ptr();
            const int len = vs.length();
            int d = 0;
            const bool is_text = (len >= 2)
                && (static_cast<unsigned char>(p[0]) == 0x5B)
                && (static_cast<unsigned char>(p[len - 1]) == 0x5D);
            if (is_text) {
              const char *q = p + 1;
              const char *e = p + len - 1;
              char buf[64];
              while (q < e) {
                while (q < e && (static_cast<unsigned char>(*q) == 0x20
                                 || static_cast<unsigned char>(*q) == 0x2C)) { q++; }
                int bi = 0;
                while (q < e && static_cast<unsigned char>(*q) != 0x2C && bi < 63) { buf[bi++] = *q++; }
                buf[bi] = 0;
                if (bi > 0) { vecs.push_back(static_cast<float>(atof(buf))); d++; }
              }
            } else if (len >= 4 && (len % 4) == 0) {
              d = len / 4;
              const float *f = reinterpret_cast<const float *>(p);
              vecs.insert(vecs.end(), f, f + d);
            } else {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("batch_knn: bad vector payload", K(ret), K(len));
            }
            if (OB_SUCC(ret)) {
              if (dim == 0) { dim = d; }
              if (d != dim) {
                ret = OB_INVALID_ARGUMENT;
                LOG_WARN("batch_knn: dim mismatch", K(ret), K(d), K(dim));
              } else {
                ids.push_back(id);
              }
            }
          }
        }
      }
      if (OB_ITER_END == ret) { ret = OB_SUCCESS; }
    }
  }
  return ret;
}

int ObDBMSVectorMySql::batch_knn(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result)
{
  UNUSED(result);
  int ret = OB_SUCCESS;
  CK(OB_LIKELY(4 == params.count()));
  if (OB_SUCC(ret)
      && (!params.at(0).is_varchar() || !params.at(1).is_varchar()
          || !params.at(2).is_int32() || !params.at(3).is_varchar())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument for batch_knn", KR(ret));
  }
  if (OB_SUCC(ret)) {
    common::ObString index_table = params.at(0).get_varchar();
    common::ObString probe_table = params.at(1).get_varchar();
    int64_t topk = params.at(2).get_int();
    common::ObString out_table = params.at(3).get_varchar();
    auto *session = ctx.exec_ctx_->get_my_session();
    if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null session", K(ret));
    } else {
      common::ObString db = session->get_database_name();
      std::vector<float> base;
      std::vector<float> query;
      std::vector<int64_t> base_ids;
      std::vector<int64_t> probe_ids;
      int bdim = 0;
      int qdim = 0;
      if (db.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("batch_knn: no database selected", K(ret));
      } else if (OB_FAIL(batch_knn_read_vectors(db, index_table, base, base_ids, bdim))) {
      } else if (OB_FAIL(batch_knn_read_vectors(db, probe_table, query, probe_ids, qdim))) {
      } else if (base_ids.empty() || probe_ids.empty() || bdim == 0 || bdim != qdim) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("batch_knn: empty or dim mismatch", K(ret), K(base_ids.size()), K(probe_ids.size()), K(bdim), K(qdim));
      } else {
        if (topk > static_cast<int64_t>(base_ids.size())) { topk = static_cast<int64_t>(base_ids.size()); }
        const long n = static_cast<long>(base_ids.size());
        const long nq = static_cast<long>(probe_ids.size());
        std::vector<unsigned> off(static_cast<size_t>(nq) * topk);
        std::vector<float> dist(static_cast<size_t>(nq) * topk);
        const long served = common::obvsag::cuvs_batch_knn(base.data(), n, bdim,
                                query.data(), nq, topk, off.data(), dist.data());
        if (served != nq) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("batch_knn: GPU did not serve (need OB_VSAG_USE_CUVS=1)", K(ret), K(served), K(nq));
        } else {
          int64_t affected = 0;
          common::ObSqlString del;
          if (OB_FAIL(del.assign_fmt("DELETE FROM `%.*s`.`%.*s`",
                         db.length(), db.ptr(), out_table.length(), out_table.ptr()))) {
          } else if (OB_FAIL(GCTX.sql_proxy_->write(del.ptr(), 0, affected))) {
            LOG_WARN("batch_knn: clear out_table failed (create it first)", K(ret), K(del));
          }
          const long CHUNK = 400;
          long q = 0;
          while (OB_SUCC(ret) && q < nq) {
            common::ObSqlString ins;
            if (OB_FAIL(ins.assign_fmt("INSERT INTO `%.*s`.`%.*s`(probe_id,neighbor_id,distance,rk) VALUES ",
                           db.length(), db.ptr(), out_table.length(), out_table.ptr()))) {
              break;
            }
            long rows_in_stmt = 0;
            for (; OB_SUCC(ret) && q < nq && rows_in_stmt < CHUNK; ++q) {
              for (long i = 0; OB_SUCC(ret) && i < topk; ++i) {
                const size_t pidx = static_cast<size_t>(q) * topk + i;
                const unsigned o = off[pidx];
                const int64_t nid = (o < base_ids.size()) ? base_ids[o] : -1;
                if (OB_FAIL(ins.append_fmt("%s(%ld,%ld,%.6f,%ld)",
                               (rows_in_stmt > 0) ? "," : "",
                               static_cast<long>(probe_ids[q]), static_cast<long>(nid),
                               dist[pidx], static_cast<long>(i)))) {
                } else {
                  rows_in_stmt++;
                }
              }
            }
            if (OB_SUCC(ret) && OB_FAIL(GCTX.sql_proxy_->write(ins.ptr(), 0, affected))) {
              LOG_WARN("batch_knn: insert failed", K(ret));
            }
          }
          if (OB_SUCC(ret)) { LOG_INFO("batch_knn done", K(n), K(nq), K(topk), K(bdim)); }
        }
      }
    }
  }
  return ret;
}

}
}
