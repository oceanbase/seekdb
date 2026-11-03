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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/ddl/ob_analyze_stmt_resolver.h"
#include "sql/resolver/ddl/ob_analyze_stmt.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;
namespace oceanbase
{
namespace sql
{

ObAnalyzeStmtResolver::ObAnalyzeStmtResolver(ObResolverParams &params)
  : ObDDLResolver(params)
{
}

ObAnalyzeStmtResolver::~ObAnalyzeStmtResolver()
{
}

int ObAnalyzeStmtResolver::resolve(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;
  ObAnalyzeStmt *analyze_stmt = NULL;
  uint64_t parallel_degree = 1;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(analyze_stmt = create_stmt<ObAnalyzeStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("failed to create analyze stmt", K(ret));
  } else if (OB_FAIL(session_info_->get_force_parallel_query_dop(parallel_degree))) {
  } else if (parse_tree.kind() == peg::NodeKind::Analyze) {
    OZ (resolve_analyze_table(parse_tree, *analyze_stmt));
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_SUCC(ret)) {
    analyze_stmt->set_degree(parallel_degree);
    stmt_ = analyze_stmt;
    ObSEArray<ObColumnStatParam, 4> new_column_params;
    for (int64_t j = 0; OB_SUCC(ret) && j < analyze_stmt->get_tables().count(); ++j) {
      ObAnalyzeTableInfo &table = analyze_stmt->get_tables().at(j);
      new_column_params.reuse();
      for (int64_t i = 0 ; OB_SUCC(ret) && i < table.get_column_params().count(); ++i) {
        if (table.get_column_params().at(i).need_col_stat()) {
          if (OB_FAIL(new_column_params.push_back(table.get_column_params().at(i)))) {
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(table.get_column_params().assign(new_column_params))) {
        }
      }
    }
  }
  return ret;
}

int ObAnalyzeStmtResolver::resolve_analyze_table(const peg::Node &statement,
                                                 ObAnalyzeStmt &analyze_stmt)
{
  int ret = OB_SUCCESS;
  const auto definition = peg::analyze_definition(statement);
  if (!definition.bucket_count.empty()) {
    const auto &count = definition.bucket_count[0];
    if (count.kind() != peg::NodeKind::Int
        || peg::integer_literal_value(count) < 1
        || peg::integer_literal_value(count) > 1024) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "histogram bucket count (must be between 1 and 1024)");
    }
  }
  for (const auto &relation : definition.relations) {
    if (OB_FAIL(ret)) { break; }
    OZ (resolve_table_info(&relation, analyze_stmt));
  }
  OZ (resolve_partition_info(analyze_stmt));
  if (definition.action == peg::AnalyzeAction::Gather) {
    OZ (resolve_default_column_info(analyze_stmt));
  } else if (OB_SUCC(ret)) {
    ObAnalyzeTableInfo &table_info = analyze_stmt.get_tables().at(0);
    const ObTableSchema *table_schema = nullptr;
    const bool is_drop = definition.action == peg::AnalyzeAction::DropHistogram;
    const int64_t bucket_count = is_drop ? 0 : !definition.bucket_count.empty()
        ? static_cast<int64_t>(peg::integer_literal_value(definition.bucket_count[0]))
        : ObColumnStatParam::DEFAULT_HISTOGRAM_BUCKET_NUM;
    if (is_drop) {
      analyze_stmt.set_is_drop();
    }
    if (OB_FAIL(schema_checker_->get_table_schema(table_info.get_table_id(), table_schema))) {
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
    }
    for (const auto &column : definition.columns) {
      if (OB_FAIL(ret)) { break; }
      const auto text = column.text();
      const ObString column_name(syntax_text(text));
      const ObColumnSchemaV2 *column_schema = table_schema->get_column_schema(column_name);
      if (OB_ISNULL(column_schema)) {
        ret = OB_ERR_COLUMN_NOT_FOUND;
      } else {
        ObColumnStatParam *column_param = table_info.get_column_param(column_schema->get_column_id());
        if (column_param != nullptr && column_param->is_valid_opt_col()) {
          column_param->set_need_basic_stat();
          column_param->bucket_num_ = bucket_count;
          column_param->set_size_manual();
        }
      }
    }
    if (OB_SUCC(ret) && !is_drop) {
      constexpr int64_t sample_rows_per_bucket = 512;
      ObAnalyzeSampleInfo sample_info;
      sample_info.is_sample_ = true;
      sample_info.sample_type_ = SampleType::RowSample;
      sample_info.sample_value_ = bucket_count * sample_rows_per_bucket;
      analyze_stmt.set_sample_info(sample_info);
    }
  }
  return ret;
}









int ObAnalyzeStmtResolver::resolve_table_info(const peg::Node *table_node,
                                              ObAnalyzeStmt &analyze_stmt)
{
  int ret = OB_SUCCESS;
  ObString table_name;
  ObString database_name;
  ObNameCaseMode case_mode = ObNameCaseMode::OB_NAME_CASE_INVALID;
  const ObTableSchema *table_schema = NULL;
  
  uint64_t database_id = OB_INVALID_ID;
  if (OB_ISNULL(table_node) || OB_ISNULL(schema_checker_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(resolve_table_relation_node(table_node, table_name, database_name))) {
  } else if (OB_FAIL(schema_checker_->get_database_id(database_name, database_id))) {
  } else if (OB_FAIL(schema_checker_->get_table_schema( database_name,
                                                       table_name, false, table_schema))){
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
  } else if (OB_FAIL(analyze_stmt.add_table(database_name, database_id, table_name,
                           table_schema->get_table_id(),
                           table_schema->get_table_type()))) {
  } else {
    ObAnalyzeTableInfo &last_table = analyze_stmt.get_tables().at(analyze_stmt.get_tables().count() - 1);
    if (OB_FAIL(pl::ObDbmsStats::init_column_stat_params(*allocator_,
                                                        *schema_checker_->get_schema_guard(),
                                                        *table_schema,
                                                        last_table.get_column_params()))) {
    } else if (OB_FAIL(pl::ObDbmsStats::init_column_group_stat_param(*table_schema,
                                                                     last_table.get_column_group_params()))) {
    } else {
    }
  }
  return ret;
}

int ObAnalyzeStmtResolver::resolve_partition_info(ObAnalyzeStmt &analyze_stmt)
{
  int ret = OB_SUCCESS;
  ObIArray<ObAnalyzeTableInfo> &tables = analyze_stmt.get_tables();
  for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
    if (OB_FAIL(inner_resolve_partition_info(tables.at(i)))) {
    }
  }
  return ret;
}

int ObAnalyzeStmtResolver::inner_resolve_partition_info(ObAnalyzeTableInfo &table_info)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  int64_t table_id = table_info.get_table_id();
  ObSEArray<PartInfo, 4> part_infos;
  ObSEArray<PartInfo, 32> subpart_infos;
  ObSEArray<int64_t, 4> part_ids;
  ObSEArray<int64_t, 4> subpart_ids;
  if (OB_ISNULL(schema_checker_) || OB_ISNULL(params_.allocator_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_checker_->get_table_schema( table_id, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObDbmsStatsUtils::get_part_infos(*table_schema,
                                                      *params_.allocator_,
                                                      part_infos,
                                                      subpart_infos,
                                                      part_ids,
                                                      subpart_ids))) {
  } else if (OB_FAIL(table_info.set_all_part_infos(part_infos))) {
  } else if (OB_FAIL(table_info.set_all_subpart_infos(subpart_infos))) {
  } else {
    table_info.set_part_level(table_schema->get_part_level());
    OZ (table_info.set_part_infos(part_infos));
    OZ (table_info.set_subpart_infos(subpart_infos));
  }
  return ret;
}

int ObAnalyzeStmtResolver::resolve_default_column_info(ObAnalyzeStmt &analyze_stmt)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  ObIArray<ObAnalyzeTableInfo> &tables = analyze_stmt.get_tables();
  if (OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
    bool is_hist_subpart = false;
    if (OB_FAIL(schema_checker_->get_table_schema(tables.at(i).get_table_id(), table_schema))) {
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(pl::ObDbmsStats::set_default_column_params(tables.at(i).get_column_params()))) {
    } else {
      if (share::schema::ObPartitionLevel::PARTITION_LEVEL_TWO == table_schema->get_part_level()) {
        is_hist_subpart = is_range_part(table_schema->get_sub_part_option().get_part_func_type()) ||
                           is_list_part(table_schema->get_sub_part_option().get_part_func_type());
      }
      tables.at(i).set_gather_subpart_hist(is_hist_subpart);
    }
  }
  return ret;
}


} /* namespace sql */
} /* namespace oceanbase */
