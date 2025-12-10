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

#include "ob_hybrid_search_executor.h"
#include "ob_hybrid_search_fusion_engine.h"
#include "storage/vector_index/cmd/ob_vector_refresh_index_executor.h"
#include "lib/json_type/ob_json_base.h"
#include "lib/json_type/ob_json_tree.h"

#define USING_LOG_PREFIX SHARE

namespace oceanbase {
namespace share {

ObHybridSearchExecutor::ObHybridSearchExecutor()
    : ctx_(NULL), allocator_("HybridSearch") {}

ObHybridSearchExecutor::~ObHybridSearchExecutor() {}

int ObHybridSearchExecutor::init_search_arg(const ObHybridSearchArg &arg) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(arg));
  }
  search_arg_.search_params_ = arg.search_params_;
  search_arg_.search_type_ = arg.search_type_;
  search_arg_.table_name_ = arg.table_name_;
  search_arg_.search_type_ = arg.search_type_;
  result_type_ = SearchResultType::SQL_RESULT;

  return ret;
}

int ObHybridSearchExecutor::init(const pl::ObPLExecCtx &ctx, const ObHybridSearchArg &arg) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx.exec_ctx_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("exec context is not initialized", K(ret));
  } else if (OB_FAIL(init(ctx.exec_ctx_, arg))) {
    LOG_WARN("fail to init", KR(ret));
  }
  return ret;
}

int ObHybridSearchExecutor::init(sql::ObExecContext *ctx, const ObHybridSearchArg &arg) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx->get_my_session())) {
    ret = OB_NOT_INIT;
    LOG_WARN("session is not initialized", K(ret));
  } else if (init_search_arg(arg)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(arg));
  } else {
    ctx_ = ctx;
    session_info_ = ctx_->get_my_session();
    tenant_id_ = session_info_->get_effective_tenant_id();
  }
  return ret;
}

int ObHybridSearchExecutor::execute_search(ObObj &query_res) {
  int ret = OB_SUCCESS;
  ObString query_sql;
  if (OB_ISNULL(ctx_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("exec context is not initialized", K(ret));
  } else if (OB_FAIL(do_get_sql(search_arg_.search_params_, query_sql, true))) {
    LOG_WARN("fail to do get sql", KR(ret));
  } else {
    common::ObMySQLProxy* sql_proxy = ctx_->get_sql_proxy();
    SMART_VAR(ObMySQLProxy::MySQLResult, result) {
      if (OB_FAIL(sql_proxy->read(result, tenant_id_, query_sql.ptr()))) {
        LOG_WARN("execute query failed", K(ret), K(query_sql), K(tenant_id_));
      } else if (OB_NOT_NULL(result.get_result())) {
        if (OB_SUCCESS == (ret = result.get_result()->next())) {
          // Step 1: Parse FTS and Vector results from SQL result
          common::ObSEArray<ObHybridSearchResult, 64> fts_results;
          common::ObSEArray<ObHybridSearchResult, 64> vector_results;
          
          if (OB_FAIL(parse_hybrid_search_result(result.get_result(), fts_results, vector_results))) {
            LOG_WARN("fail to parse hybrid search result", K(ret));
          } else if (OB_FAIL(apply_fusion_and_convert_to_json(fts_results, vector_results, 
                                                              search_arg_.search_params_, query_res))) {
            LOG_WARN("fail to apply fusion and convert to json", K(ret));
          }
        } else if (OB_ITER_END == ret) {
          LOG_INFO("no result return!", K(ret), K(tenant_id_));
          query_res.set_null();
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to get next", K(ret), K(tenant_id_));
        }
      }
    }
  }

  return ret;
}

int ObHybridSearchExecutor::execute_get_sql(ObString &sql_result) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(do_get_sql(search_arg_.search_params_, sql_result))) {
      LOG_WARN("fail to do get sql", KR(ret));
  }
  return ret;
}

int ObHybridSearchExecutor::parse_hybrid_search_result(
    const common::sqlclient::ObMySQLResult *result,
    common::ObIArray<ObHybridSearchResult> &fts_results,
    common::ObIArray<ObHybridSearchResult> &vector_results) {
  int ret = OB_SUCCESS;
  
  if (OB_ISNULL(result)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("result is null", K(ret));
  } else {
    // Extract results from the SQL query result
    // The SQL generates FULL OUTER JOIN of FTS and Vector search results
    // Each row contains: doc_id, fts_rank, vector_rank, fts_score, vector_score, fts_matched, vector_matched
    
    while (OB_SUCC(ret)) {
      uint64_t doc_id = 0;
      double fts_score = 0.0;
      double vector_score = 0.0;
      int64_t fts_rank = -1;
      int64_t vector_rank = -1;
      
      // Try to extract FTS-only result (FTS matched, Vector is NULL)
      if (OB_FAIL(result->get_uint("doc_id", doc_id))) {
        if (OB_ERR_NULL_VALUE == ret || OB_ERR_COLUMN_NOT_FOUND == ret) {
          ret = OB_SUCCESS;
          break;  // No more rows
        }
        LOG_WARN("fail to extract doc_id", K(ret));
        break;
      } else {
        // Extract FTS rank and score
        ObHybridSearchResult hybrid_result;
        hybrid_result.doc_id_ = doc_id;
        
        // Try to get FTS rank (will be NULL if no FTS match)
        if (OB_FAIL(result->get_int("fts_rank", fts_rank))) {
          if (OB_ERR_NULL_VALUE == ret) {
            fts_rank = -1;
            ret = OB_SUCCESS;  // FTS not matched
          } else if (OB_ERR_COLUMN_NOT_FOUND != ret) {
            LOG_WARN("fail to extract fts_rank", K(ret));
            break;
          }
        }
        
        // Try to get Vector rank (will be NULL if no Vector match)
        if (OB_SUCC(ret)) {
          if (OB_FAIL(result->get_int("vector_rank", vector_rank))) {
            if (OB_ERR_NULL_VALUE == ret) {
              vector_rank = -1;
              ret = OB_SUCCESS;  // Vector not matched
            } else if (OB_ERR_COLUMN_NOT_FOUND != ret) {
              LOG_WARN("fail to extract vector_rank", K(ret));
              break;
            }
          }
        }
        
        // Try to get FTS score
        if (OB_SUCC(ret)) {
          if (OB_FAIL(result->get_double("fts_score", fts_score))) {
            if (OB_ERR_NULL_VALUE == ret) {
              fts_score = 0.0;
              ret = OB_SUCCESS;
            } else if (OB_ERR_COLUMN_NOT_FOUND != ret) {
              LOG_WARN("fail to extract fts_score", K(ret));
              break;
            }
          }
        }
        
        // Try to get Vector score
        if (OB_SUCC(ret)) {
          if (OB_FAIL(result->get_double("vector_score", vector_score))) {
            if (OB_ERR_NULL_VALUE == ret) {
              vector_score = 0.0;
              ret = OB_SUCCESS;
            } else if (OB_ERR_COLUMN_NOT_FOUND != ret) {
              LOG_WARN("fail to extract vector_score", K(ret));
              break;
            }
          }
        }
        
        // Populate result based on which search matched
        if (OB_SUCC(ret)) {
          if (fts_rank >= 0 && vector_rank >= 0) {
            // Both FTS and Vector matched
            hybrid_result.fts_rank_ = fts_rank;
            hybrid_result.vector_rank_ = vector_rank;
            hybrid_result.fts_score_ = fts_score;
            hybrid_result.vector_score_ = vector_score;
            hybrid_result.source_flag_ = 3;  // Both sources
            if (OB_FAIL(fts_results.push_back(hybrid_result))) {
              LOG_WARN("fail to push fts result", K(ret));
              break;
            }
            if (OB_FAIL(vector_results.push_back(hybrid_result))) {
              LOG_WARN("fail to push vector result", K(ret));
              break;
            }
          } else if (fts_rank >= 0) {
            // FTS only
            hybrid_result.fts_rank_ = fts_rank;
            hybrid_result.vector_rank_ = -1;
            hybrid_result.fts_score_ = fts_score;
            hybrid_result.vector_score_ = 0.0;
            hybrid_result.source_flag_ = 1;  // FTS only
            if (OB_FAIL(fts_results.push_back(hybrid_result))) {
              LOG_WARN("fail to push fts result", K(ret));
              break;
            }
          } else if (vector_rank >= 0) {
            // Vector only
            hybrid_result.fts_rank_ = -1;
            hybrid_result.vector_rank_ = vector_rank;
            hybrid_result.fts_score_ = 0.0;
            hybrid_result.vector_score_ = vector_score;
            hybrid_result.source_flag_ = 2;  // Vector only
            if (OB_FAIL(vector_results.push_back(hybrid_result))) {
              LOG_WARN("fail to push vector result", K(ret));
              break;
            }
          }
        }
      }
      
      // Get next row
      ret = const_cast<common::sqlclient::ObMySQLResult*>(result)->next();
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else if (OB_FAIL(ret)) {
        LOG_WARN("fail to get next result", K(ret));
        break;
      }
    }
  }
  
  return ret;
}

int ObHybridSearchExecutor::apply_fusion_and_convert_to_json(
    const common::ObIArray<ObHybridSearchResult> &fts_results,
    const common::ObIArray<ObHybridSearchResult> &vector_results,
    const ObString &search_params_str,
    ObObj &query_res) {
  int ret = OB_SUCCESS;
  
  if (OB_ISNULL(ctx_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("exec context not initialized", K(ret));
  } else {
    // Step 1: Initialize fusion engine
    ObHybridSearchFusionEngine fusion_engine;
    common::ObIAllocator &allocator = ctx_->get_allocator();
    
    // Step 2: Determine fusion strategy and parameters from search_params_str
    // Parse fusion configuration from search parameters
    ObRRFConfig rrf_config(60, 1000);  // Default: rank_constant=60, rank_window_size=1000
    
    // Extract RRF parameters from search_params_str if provided
    // Expected format: search_params contains "rrf_query_param" with rank_constant and rank_window_size
    if (OB_FAIL(parse_rrf_config_from_params(search_params_str, rrf_config))) {
      // If parsing fails, use default configuration
      LOG_INFO("fail to parse rrf config from params, using default config", K(ret));
      ret = OB_SUCCESS;  // Reset error to continue with default config
    }
    
    if (OB_FAIL(fusion_engine.init(ObHybridSearchFusionEngine::FusionStrategy::RRF, 
                                     &rrf_config, allocator))) {
      LOG_WARN("fail to init fusion engine", K(ret));
    } else {
      // Step 3: Feed FTS and Vector results to fusion engine
      if (OB_FAIL(fusion_engine.feed_fts_results(fts_results))) {
        LOG_WARN("fail to feed fts results to fusion engine", K(ret));
      } else if (OB_FAIL(fusion_engine.feed_vector_results(vector_results))) {
        LOG_WARN("fail to feed vector results to fusion engine", K(ret));
      } else if (OB_FAIL(fusion_engine.execute_fusion())) {
        // Step 4: Execute fusion algorithm (RRF in this case)
        LOG_WARN("fail to execute fusion", K(ret));
      } else {
        // Step 5: Get fused results
        common::ObSEArray<ObHybridSearchResult, 64> fused_results;
        if (OB_FAIL(fusion_engine.get_fused_results(fused_results))) {
          LOG_WARN("fail to get fused results", K(ret));
        } else {
          // Step 6: Convert fused results to JSON format
          common::ObJsonObject response_json(&allocator);
          common::ObJsonArray hits_array(&allocator);
          
          // Add results array
          for (int64_t i = 0; OB_SUCC(ret) && i < fused_results.count(); ++i) {
            const ObHybridSearchResult &result = fused_results.at(i);
            common::ObJsonObject result_obj(&allocator);
            
            // Add doc_id
            if (OB_FAIL(result_obj.add(common::ObString::make_string("doc_id"), 
                                        static_cast<int64_t>(result.doc_id_)))) {
              LOG_WARN("fail to add doc_id", K(ret));
              break;
            }
            
            // Add final score
            if (OB_FAIL(result_obj.add(common::ObString::make_string("score"), 
                                        result.final_score_))) {
              LOG_WARN("fail to add score", K(ret));
              break;
            }
            
            // Add FTS score if available
            if (result.fts_rank_ >= 0) {
              if (OB_FAIL(result_obj.add(common::ObString::make_string("fts_score"), 
                                          result.fts_score_))) {
                LOG_WARN("fail to add fts_score", K(ret));
                break;
              }
              if (OB_FAIL(result_obj.add(common::ObString::make_string("fts_rank"), 
                                          result.fts_rank_))) {
                LOG_WARN("fail to add fts_rank", K(ret));
                break;
              }
            }
            
            // Add Vector score if available
            if (result.vector_rank_ >= 0) {
              if (OB_FAIL(result_obj.add(common::ObString::make_string("vector_score"), 
                                          result.vector_score_))) {
                LOG_WARN("fail to add vector_score", K(ret));
                break;
              }
              if (OB_FAIL(result_obj.add(common::ObString::make_string("vector_rank"), 
                                          result.vector_rank_))) {
                LOG_WARN("fail to add vector_rank", K(ret));
                break;
              }
            }
            
            // Add source flag
            if (OB_FAIL(result_obj.add(common::ObString::make_string("source"), 
                                        static_cast<int64_t>(result.source_flag_)))) {
              LOG_WARN("fail to add source flag", K(ret));
              break;
            }
            
            // Add result object to hits array
            if (OB_FAIL(hits_array.append(&result_obj))) {
              LOG_WARN("fail to append result to hits array", K(ret));
              break;
            }
          }
          
          if (OB_SUCC(ret)) {
            // Add hits array to response
            if (OB_FAIL(response_json.add(common::ObString::make_string("hits"), 
                                           &hits_array))) {
              LOG_WARN("fail to add hits array to response", K(ret));
            } else {
              // Add metadata
              if (OB_FAIL(response_json.add(common::ObString::make_string("total"), 
                                             static_cast<int64_t>(fused_results.count())))) {
                LOG_WARN("fail to add total count", K(ret));
              } else {
                // Convert JSON object to string and set as result
                common::ObStringBuffer json_str(&allocator);
                if (OB_FAIL(response_json.print(json_str, 0))) {
                  LOG_WARN("fail to print json object", K(ret));
                } else {
                  // Create ObObj from JSON string
                  ObString json_result(json_str.length(), json_str.ptr());
                  if (OB_FAIL(common::deep_copy_obj(allocator, 
                                                     ObObj(json_result), query_res))) {
                    LOG_WARN("fail to deep copy json result", K(ret));
                  }
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

int ObHybridSearchExecutor::parse_rrf_config_from_params(
    const ObString &search_params_str,
    ObRRFConfig &rrf_config) {
  int ret = OB_SUCCESS;
  
  if (OB_ISNULL(search_params_str.ptr()) || search_params_str.length() == 0) {
    // Empty params string, use default configuration
    return OB_SUCCESS;
  }
  
  // Parse rrf_query_param from JSON search parameters
  // Expected format: {..., "rrf_query_param": {"rank_constant": 60, "rank_window_size": 1000}, ...}
  ObIJsonBase *json_base = nullptr;
  if (OB_FAIL(common::ObJsonBaseFactory::get_json_base(&ctx_->get_allocator(), 
                                                        search_params_str,
                                                        common::ObJsonInType::JSON_TREE,
                                                        common::ObJsonInType::JSON_TREE,
                                                        json_base))) {
    LOG_WARN("fail to parse search params as json", K(ret), K(search_params_str));
    // Not a valid JSON, return success to use default config
    ret = OB_SUCCESS;
  } else if (OB_ISNULL(json_base)) {
    // JSON parse result is null, use default config
    ret = OB_SUCCESS;
  } else {
    common::ObJsonObject *json_obj = static_cast<common::ObJsonObject*>(json_base);
    common::ObIJsonBase *rrf_param_base = nullptr;
    
    // Try to get rrf_query_param object
    if (OB_FAIL(json_obj->get_object_value(common::ObString::make_string("rrf_query_param"), 
                                             rrf_param_base))) {
      if (OB_ERR_JSON_PATH_EXPRESSION_ERROR == ret) {
        // rrf_query_param not found, use default configuration
        LOG_INFO("rrf_query_param not found in search params, using default config");
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to get rrf_query_param from search params", K(ret));
      }
    } else if (OB_ISNULL(rrf_param_base)) {
      // rrf_param_base is null, use default config
      ret = OB_SUCCESS;
    } else {
      common::ObJsonObject *rrf_param_obj = static_cast<common::ObJsonObject*>(rrf_param_base);
      
      // Extract rank_constant
      common::ObIJsonBase *rank_const_base = nullptr;
      if (OB_FAIL(rrf_param_obj->get_object_value(
          common::ObString::make_string("rank_constant"), rank_const_base))) {
        if (OB_ERR_JSON_PATH_EXPRESSION_ERROR != ret) {
          LOG_WARN("fail to get rank_constant from rrf_query_param", K(ret));
        }
        ret = OB_SUCCESS;  // Not critical, use default
      } else if (OB_NOT_NULL(rank_const_base)) {
        common::ObJsonNumber *rank_const_num = 
            static_cast<common::ObJsonNumber*>(rank_const_base);
        rrf_config.rank_constant_ = static_cast<int64_t>(rank_const_num->get_double());
        LOG_INFO("parsed rank_constant from rrf_query_param", 
                 K(rrf_config.rank_constant_));
      }
      
      // Extract rank_window_size
      common::ObIJsonBase *window_size_base = nullptr;
      if (OB_FAIL(rrf_param_obj->get_object_value(
          common::ObString::make_string("rank_window_size"), window_size_base))) {
        if (OB_ERR_JSON_PATH_EXPRESSION_ERROR != ret) {
          LOG_WARN("fail to get rank_window_size from rrf_query_param", K(ret));
        }
        ret = OB_SUCCESS;  // Not critical, use default
      } else if (OB_NOT_NULL(window_size_base)) {
        common::ObJsonNumber *window_size_num = 
            static_cast<common::ObJsonNumber*>(window_size_base);
        rrf_config.rank_window_size_ = static_cast<int64_t>(window_size_num->get_double());
        LOG_INFO("parsed rank_window_size from rrf_query_param", 
                 K(rrf_config.rank_window_size_));
      }
    }
  }
  
  return ret;
}

int ObHybridSearchExecutor::do_get_sql(const ObString &search_params_str,
                                       ObString &sql_result, bool need_wrap_result /*= false*/) {
  int ret = OB_SUCCESS;
  share::ObQueryReqFromJson *query_req = nullptr;

  if (OB_ISNULL(ctx_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("exec context is not initialized", K(ret));
  } else {
    if (OB_FAIL(parse_search_params(search_params_str, query_req, need_wrap_result))) {
      LOG_WARN("fail to parse search params", KR(ret));
    } else if (OB_ISNULL(query_req)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("query request is null", KR(ret));
    } else {
      char *buf = NULL;
      int64_t res_len = 0;
      bool is_complete = false;
      ObIAllocator &alloc = ctx_->get_allocator();
      for (int64_t i = 1; OB_SUCC(ret) && !is_complete && i <= 1024; i = i * 2) {
        const int64_t length = OB_MAX_SQL_LENGTH * i;
        res_len = 0;
        if (OB_ISNULL(buf = static_cast<char*>(alloc.alloc(length)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to alloc memory for sql", K(ret), K(length));
        } else if (FALSE_IT(MEMSET(buf, 0, length))) {
        } else if (OB_FAIL(query_req->translate(buf, length, res_len))) {
          LOG_WARN("fail to translate to sql", KR(ret));
        }
        if (OB_SUCC(ret)) {
          is_complete = true;
          sql_result.assign_ptr(buf, res_len);
        } else if (OB_SIZE_OVERFLOW == ret) {
          // retry
          ret = OB_SUCCESS;
        }
      }
    }
  }
  return ret;
}

int ObHybridSearchExecutor::parse_search_params(
    const ObString &search_params_str, share::ObQueryReqFromJson *&query_req, bool need_wrap_result) {

  int ret = OB_SUCCESS;
  ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
  ObCollationType cs_type = CS_TYPE_INVALID;
  ObString table_name;
  ObString database_name;
  if (OB_ISNULL(search_params_str.ptr()) || search_params_str.length() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("search_params_str is invalid", K(ret), K(search_params_str));
  } else if (OB_FAIL(session_info_->get_name_case_mode(case_mode))) {
    LOG_WARN("fail to get name case mode", KR(ret));
  } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
    LOG_WARN("fail to get collation_connection", KR(ret));
  } else if (OB_FAIL(ObVectorRefreshIndexExecutor::resolve_table_name(
              cs_type, case_mode, lib::is_oracle_mode(), search_arg_.table_name_,
              database_name, table_name))) {
    LOG_WARN("fail to resolve table name", KR(ret), K(cs_type), K(case_mode), K(search_arg_.table_name_));
  } else if (database_name.empty() && FALSE_IT(database_name = session_info_->get_database_name())) {
  } else if (OB_UNLIKELY(database_name.empty())) {
    ret = OB_ERR_NO_DB_SELECTED;
    LOG_WARN("No database selected", KR(ret));
  } else {
    ObESQueryParser parser(allocator_, need_wrap_result, &table_name, &database_name);
    if (OB_FAIL(construct_column_index_info(allocator_, parser))) {
      LOG_WARN("fail to construnct column index info", KR(ret), K(search_params_str));
    } else if (OB_FAIL(parser.parse(search_params_str, query_req))) {
      LOG_WARN("fail to parse search params", KR(ret), K(search_params_str));
    }
  }
  return ret;
}

int ObHybridSearchExecutor::construct_column_index_info(ObIAllocator &alloc, ObESQueryParser &parser)
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard *schema_guard = NULL;
  const ObTableSchema *data_table_schema = NULL;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
  ObCStringHelper helper;
  const ObString &database_name = parser.get_database_name();
  const ObString &table_name = parser.get_table_name();
  ColumnIndexNameMap &column_index_info = parser.get_index_name_map();
  ObIArray<ObString> &col_names = parser.get_user_column_names();

  if (OB_ISNULL(schema_guard = ctx_->get_virtual_table_ctx().schema_guard_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema guard is null", KR(ret));
  } else if (OB_FAIL(schema_guard->get_table_schema(tenant_id_, database_name, table_name,
                  false, data_table_schema))) {
    LOG_WARN("failed to get table id", K(ret), K(database_name), K(table_name));
  } else if (data_table_schema == NULL) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(database_name), helper.convert(table_name));
  } else if (!data_table_schema->is_table_with_hidden_pk_column()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "table with user provided primary key");
    LOG_WARN("table with user provided primary key isn't supported", K(ret));
  } else if (OB_FAIL(data_table_schema->get_simple_index_infos(simple_index_infos))) {
    LOG_WARN("fail to get simple index infos failed", K(ret));
  } else if (OB_FAIL(get_basic_column_names(data_table_schema, col_names))) {
    LOG_WARN("fail to get all column names", K(ret));
  } else if (OB_FAIL(get_partition_info(data_table_schema, parser))) {
    LOG_WARN("fail to get partition column names and init alias exprs", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
      const ObTableSchema *index_table_schema = nullptr;
      if (OB_FAIL(schema_guard->get_table_schema(tenant_id_, simple_index_infos.at(i).table_id_, index_table_schema))) {
        LOG_WARN("fail to get index_table_schema", K(ret), K(tenant_id_), "table_id", simple_index_infos.at(i).table_id_);
      } else if (OB_ISNULL(index_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("index table schema should not be null", K(ret), K(simple_index_infos.at(i).table_id_));
      } else if (index_table_schema->is_built_in_index()) {
        // skip built in vector index table
      } else {
        // handle delta_buffer_table index table
        const ObRowkeyInfo &rowkey_info = index_table_schema->get_rowkey_info();
        for (int64_t j = 0; OB_SUCC(ret) && j < rowkey_info.get_size(); j++) {
          const ObRowkeyColumn *rowkey_column = rowkey_info.get_column(j);
          const int64_t column_id = rowkey_column->column_id_;
          const ObColumnSchemaV2 *col_schema = nullptr;
          if (OB_ISNULL(col_schema = index_table_schema->get_column_schema(column_id))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected col_schema, is nullptr", K(ret), K(column_id), KPC(index_table_schema));
          } else if ((index_table_schema->is_fts_index() && !col_schema->is_fulltext_column()) ||
                     (index_table_schema->is_vec_index() && col_schema->is_vec_hnsw_vid_column()) ||
                     (!index_table_schema->is_fts_index() && !index_table_schema->is_vec_index())) {
            // do nothing
          } else {
            // get generated column cascaded column id info
            // (vector index table key, like `c1` in "create table xxx vector index idx(c1)")
            ObArray<uint64_t> cascaded_column_ids;
            // get column_schema from data table using generate column id
            const ObColumnSchemaV2 *table_column = data_table_schema->get_column_schema(col_schema->get_column_id());
            ObStringBuffer column_names(&alloc);
            if (OB_ISNULL(table_column)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected table column", K(ret));
            } else if (OB_FAIL(table_column->get_cascaded_column_ids(cascaded_column_ids))) {
              LOG_WARN("failed to get cascaded column ids", K(ret));
            } else {
              for (int64_t k = 0; OB_SUCC(ret) && k < cascaded_column_ids.count(); ++k) {
                const ObColumnSchemaV2 *cascaded_column = NULL;
                ObString new_col_name;
                if (OB_ISNULL(cascaded_column = data_table_schema->get_column_schema(cascaded_column_ids.at(k)))) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("unexpected cascaded column", K(ret));
                } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
                          alloc,
                          cascaded_column->get_column_name_str(),
                          new_col_name,
                          false))) {
                  LOG_WARN("fail to generate new name with escape character", K(ret), K(cascaded_column->get_column_name_str()));
                } else if (OB_FAIL(column_names.append(new_col_name))) {
                  LOG_WARN("fail to print column name", K(ret), K(new_col_name));
                } else if (k != cascaded_column_ids.count() - 1 && OB_FAIL(column_names.append(", "))) {
                  LOG_WARN("fail to print column name", K(ret), K(new_col_name));
                }
              }
              ObString index_name;
              ObColumnIndexInfo *index_info = NULL;
              if (OB_FAIL(ret)) {
              } else if (!column_index_info.created() && OB_FAIL(column_index_info.create(simple_index_infos.count(), "HybridSearch"))) {
                LOG_WARN("fail to create column index info map", KR(ret));
              } else if (OB_FAIL(column_index_info.get_refactored(column_names.string(), index_info))) {
                if (ret == OB_HASH_NOT_EXIST) {
                  ret = OB_SUCCESS;
                  index_info = OB_NEWx(ObColumnIndexInfo, &alloc);
                  if (OB_ISNULL(index_info)) {
                    ret = OB_ALLOCATE_MEMORY_FAILED;
                    LOG_WARN("fail to create index info", K(ret));
                  } else if (OB_FAIL(ObTableSchema::get_index_name(alloc, data_table_schema->get_table_id(),
                              ObString::make_string(index_table_schema->get_table_name()), index_name))) {
                    LOG_WARN("get index table name failed", K(ret));
                  } else if (FALSE_IT(index_info->index_name_ = index_name)) {
                  } else if (FALSE_IT(index_info->index_type_ = index_table_schema->get_index_type())) {
                  } else if (index_table_schema->is_vec_index()) {
                    ObVectorIndexType index_type = ObVectorIndexType::VIT_MAX;
                    if (index_table_schema->is_vec_ivf_index()) {
                      index_type = ObVectorIndexType::VIT_IVF_INDEX;
                    } else if (index_table_schema->is_vec_hnsw_index()) {
                      index_type = ObVectorIndexType::VIT_HNSW_INDEX;
                    }
                    ObVectorIndexParam index_param;
                    if (OB_FAIL(ObVectorIndexUtil::parser_params_from_string(index_table_schema->get_index_params(), index_type, index_param))) {
                      LOG_WARN("failed to parser vec index param", K(ret), K(index_table_schema->get_index_params()));
                    } else {
                      index_info->dist_algorithm_ = index_param.dist_algorithm_;
                    }
                  }
                  if (OB_FAIL(ret)) {
                  } else if (OB_FAIL(column_index_info.set_refactored(column_names.string(), index_info))) {
                    LOG_WARN("failed to set_refactored column name", K(ret), K(column_names.string()));
                  } else {
                    LOG_INFO("column index info", K(ret), K(column_names.string()), K(index_name));
                  }
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

int ObHybridSearchExecutor::get_basic_column_names(const ObTableSchema *table_schema, ObIArray<ObString> &col_names)
{
  int ret = OB_SUCCESS;
  ObColumnIterByPrevNextID iter(*table_schema);
  const ObColumnSchemaV2 *column_schema = NULL;
  int i = 0;
  while (OB_SUCC(ret) && OB_SUCC(iter.next(column_schema))) {
    if (OB_ISNULL(column_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("The column is null", K(ret));
    } else if (column_schema->is_shadow_column() ||
               column_schema->is_invisible_column() ||
               column_schema->is_hidden()) {
      // don't show shadow columns for select * from idx
      continue;
    } else  if (OB_FAIL(col_names.push_back(column_schema->get_column_name_str()))) {
      LOG_WARN("push back column name failed", K(ret));
    }
  }
  if (ret != OB_ITER_END) {
    LOG_WARN("Failed to iterate all table columns. iter quit. ", K(ret));
  } else {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObHybridSearchExecutor::extract_partition_column_ids(const ObPartitionKeyInfo &part_key_info,
                                                         hash::ObPlacementHashSet<uint64_t, 32> &column_id_set,
                                                         ObIArray<uint64_t> &column_ids)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_INVALID_ID;
  for (int64_t i = 0; OB_SUCC(ret) && i < part_key_info.get_size(); i++) {
    if (OB_FAIL(part_key_info.get_column_id(i, column_id))) {
      LOG_WARN("failed to get column id from partition key info", K(ret), K(i));
    } else {
      int hash_ret = column_id_set.exist_refactored(column_id);
      if (OB_HASH_EXIST == hash_ret) {
      } else if (OB_HASH_NOT_EXIST == hash_ret) {
        if (OB_FAIL(column_id_set.set_refactored(column_id))) {
          LOG_WARN("failed to set column id in hash set", K(ret), K(column_id));
        } else if (OB_FAIL(column_ids.push_back(column_id))) {
          LOG_WARN("failed to push back column id", K(ret), K(column_id));
        }
      } else {
        ret = hash_ret;
        LOG_WARN("failed to check column id existence", K(ret), K(column_id));
      }
    }
  }
  return ret;
}

int ObHybridSearchExecutor::get_partition_info(const ObTableSchema *table_schema, ObESQueryParser &parser)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(table_schema));
  } else if (table_schema->get_part_level() != PARTITION_LEVEL_ZERO) {
    hash::ObPlacementHashSet<uint64_t, 32> column_id_set; // to deduplicate column ids
    ObSEArray<uint64_t, 4> column_ids;
    ObSEArray<ObString, 4> column_names;
    const ObPartitionKeyInfo &part_key_info = table_schema->get_partition_key_info();
    const ObPartitionKeyInfo &subpart_key_info = table_schema->get_subpartition_key_info();
    if (OB_FAIL(extract_partition_column_ids(part_key_info, column_id_set, column_ids))) {
      LOG_WARN("failed to extract column ids from partition key info", K(ret));
    } else if (table_schema->get_part_level() == PARTITION_LEVEL_TWO && OB_FAIL(extract_partition_column_ids(subpart_key_info, column_id_set, column_ids))) {
      LOG_WARN("failed to extract column ids from subpartition key info", K(ret));
    } else if (column_ids.count() > 0) {
      lib::ob_sort(column_ids.begin(), column_ids.end());
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); i++) {
      const ObColumnSchemaV2 *column_schema = table_schema->get_column_schema(column_ids.at(i));
      if (OB_ISNULL(column_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected column schema", K(ret), K(column_ids.at(i)));
      } else if (OB_FAIL(column_names.push_back(column_schema->get_column_name_str()))) {
        LOG_WARN("failed to push back column name", K(ret));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(parser.construct_partition_cols(column_names))) {
      LOG_WARN("failed to construct partition column and alias exprs", K(ret));
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
