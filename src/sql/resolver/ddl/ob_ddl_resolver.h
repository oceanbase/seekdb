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

#ifndef OCEANBASE_SQL_RESOLVER_DDL_OB_DDL_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_DDL_OB_DDL_RESOLVER_H_ 1
#include "sql/resolver/ob_stmt_resolver.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "lib/hash/ob_placement_hashset.h"
#include "lib/string/ob_sql_string.h"
#include "lib/worker.h"
#include "share/schema/ob_table_schema.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_struct.h"
#include "common/sql_mode/ob_sql_mode.h"
#include "sql/resolver/ddl/ob_table_stmt.h"
#include "sql/resolver/ddl/ob_alter_table_stmt.h"
#include "sql/resolver/ddl/ob_create_index_stmt.h"
#include "sql/resolver/ddl/ob_create_table_stmt.h"

namespace oceanbase
{
namespace common
{
struct ObObjCastParams;
}

namespace sql
{
typedef common::hash::ObPlacementHashSet<share::schema::ObColumnNameHashWrapper, common::OB_MAX_COLUMN_NUMBER> ObReducedVisibleColSet;
typedef common::hash::ObPlacementHashSet<share::schema::ObPartitionNameHashWrapper, common::OB_MAX_EXTENDED_PARTITION_NUM> ObPartitionNameSet;
struct PartitionInfo
{
  share::schema::ObPartitionLevel part_level_;
  share::schema::ObPartitionOption part_option_;
  share::schema::ObSubPartitionOption subpart_option_;
  common::ObSEArray<share::schema::ObPartition, 4> parts_;
  common::ObSEArray<share::schema::ObSubPartition, 2> subparts_;
  common::ObSEArray<common::ObString, 8> part_keys_;
  common::ObSEArray<ObRawExpr*, 8> part_func_exprs_;
  common::ObSEArray<ObRawExpr*, 8> range_value_exprs_;
  ObDDLStmt::array_t list_value_exprs_;
};

enum NUMCHILD {
  CREATE_TABLE_NUM_CHILD = 8,
  CREATE_TABLE_AS_SEL_NUM_CHILD = 11,
  COLUMN_DEFINITION_NUM_CHILD = 4,
  COLUMN_DEF_NUM_CHILD = 3,
  INDEX_NUM_CHILD = 5,
  CREATE_SYNONYM_NUM_CHILD = 7,
  GEN_COLUMN_DEFINITION_NUM_CHILD = 7
};

struct ObColumnResolveStat
{
  ObColumnResolveStat() {reset();}
  ~ObColumnResolveStat() {}
  void reset()
  {
    column_id_ = common::OB_INVALID_ID;
    is_primary_key_ = false;
    is_autoincrement_ = false;
    is_unique_key_ = false;
    is_set_null_ = false;
    is_set_not_null_ = false;
    is_set_default_value_ = false;
  }
  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    J_OBJ_START();
    J_KV(K(column_id_),
         K(is_primary_key_),
         K(is_autoincrement_),
         K(is_set_null_),
         K(is_set_not_null_),
         K(is_set_default_value_),
         K(is_unique_key_));
    J_OBJ_END();
    return pos;
  }
  uint64_t column_id_;
  bool is_primary_key_;
  bool is_autoincrement_;
  bool is_set_null_;
  bool is_set_not_null_;
  bool is_set_default_value_;
  bool is_unique_key_;
};

struct ObDefaultValueRes
{
  ObDefaultValueRes(common::ObObjParam &value): is_literal_(true), value_(value) {}
  bool is_literal_;
  common::ObObjParam &value_;
};

class ObDDLResolver : public ObStmtResolver
{
public:
  enum INDEX_TYPE {
    NOT_SPECIFIED = 0,
    LOCAL_INDEX = 1,
    GLOBAL_INDEX = 2
  };
  enum INDEX_KEYNAME {
    NORMAL_KEY = 0,
    UNIQUE_KEY = 1,
    SPATIAL_KEY = 2,
    FTS_KEY = 3,
    MULTI_KEY = 4,
    MULTI_UNIQUE_KEY = 5,
    VEC_KEY = 6,
  };
  enum COLUMN_NODE {
    COLUMN_REF_NODE = 0,
    COLUMN_TYPE_NODE,
    COLUMN_NAME_NODE,
  };
  enum RangeNode {
    RANGE_FUN_EXPR_NODE = 0,
    RANGE_ELEMENTS_NODE = 1,
    RANGE_SUBPARTITION_NODE = 2,
    RANGE_PARTITION_NUM_NODE = 3,
    RANGE_TEMPLATE_MARK = 4,
    RANGE_INTERVAL_NODE = 5,
  };
  enum ElementsNode {
    PARTITION_NAME_NODE = 0,
    PARTITION_ELEMENT_NODE = 1,
    ELEMENT_ATTRIBUTE_NODE = 2,
    ELEMENT_SUBPARTITION_NODE = 3,
  };
  enum ListNode {
    LIST_FUN_EXPR_NODE = 0,
    LIST_ELEMENTS_NODE = 1,
    LIST_SUBPARTITIOPPN_NODE = 2,
    LIST_PARTITION_NUM_NODE = 3,
    LIST_TEMPLATE_MARK = 4
  };
  enum HashOrKeyNode {
    HASH_FUN_EXPR_NODE = 0,
    HASH_PARTITION_NUM_NODE = 1,
    HASH_PARTITION_LIST_NODE = 2,
    HASH_SUBPARTITIOPPN_NODE = 3,
    HASH_TEMPLATE_MARK = 4,
    HASH_TABLESPACE_NODE = 5
  };
  enum ObTableOrganizationType : uint8_t {
    OB_ORGANIZATION_INVALID = 0,
    OB_INDEX_ORGANIZATION = 1,
    OB_HEAP_ORGANIZATION = 2,
    OB_ORGANIZATION_MAX
  };
  static const int NAMENODE = 1;

  static const int64_t MAX_PROGRESSIVE_MERGE_NUM = 100;
  static const int64_t MIN_BLOCK_SIZE = 1024;
  static const int64_t MAX_BLOCK_SIZE = 1048576;
  static const int64_t DEFAULT_TABLE_DOP = 1;
  explicit ObDDLResolver(ObResolverParams &params);
  virtual ~ObDDLResolver();
  static int append_fts_args(
      const share::schema::ObTableSchema &data_schema,
      const ObPartitionResolveResult &resolve_result,
      const obcall::ObCreateIndexArg &index_arg,
      bool &fts_common_aux_table_exist,
      ObIArray<ObPartitionResolveResult> &resolve_results,
      ObIArray<obcall::ObCreateIndexArg> &index_arg_list,
      ObIAllocator *allocator);
  static int append_multivalue_args(
      const share::schema::ObTableSchema &data_schema,
      const ObPartitionResolveResult &resolve_result,
      const obcall::ObCreateIndexArg &index_arg,
      bool &fts_common_aux_table_exist,
      ObIArray<ObPartitionResolveResult> &resolve_results,
      ObIArray<obcall::ObCreateIndexArg> &index_arg_list,
      ObIAllocator *allocator);
  static int check_text_length(ObCharsetType cs_type, ObCollationType co_type,
                               const char *name, ObObjType &type,
                               int32_t &length,
                               bool need_rewrite_length,
                               const bool is_byte_length = false);
  static int rewrite_text_length_mysql(ObObjType &type, int32_t &length);
  // check whether the column is allowed to be primary key.
  int check_add_column_as_pk_allowed(const ObColumnSchemaV2 &column_schema);
  static bool is_valid_prefix_key_type(
      const common::ObObjTypeClass column_type_class);
  static int check_prefix_key(
      const int32_t prefix_len,
      const share::schema::ObColumnSchemaV2 &column_schema);
  int resolve_default_value(
      ParseNode *def_node,
      ObDefaultValueRes &resolve_res);
  int resolve_sign_in_default_value(
      ObIAllocator *name_pool, ParseNode *def_val, ObDefaultValueRes &resolve_res, const bool is_neg);
  static int check_and_fill_column_charset_info(
      share::schema::ObColumnSchemaV2 &column,
      const common::ObCharsetType table_charset_type,
      const common::ObCollationType table_collation_type);
  static int check_string_column_length(
      const share::schema::ObColumnSchemaV2 &column,
      const bool is_prepare_stage=false);
  static int check_default_value_length(
      const bool is_mysql_mode,
      const share::schema::ObColumnSchemaV2 &column,
      common::ObObj &default_value);
  static int cast_default_value(
      ObSQLSessionInfo *session_info,
      common::ObObj &default_value,
      const common::ObTimeZoneInfo *tz_info,
      const common::ObString *nls_formats,
      common::ObIAllocator &allocator,
      share::schema::ObColumnSchemaV2 &column_schema,
      const ObSQLMode sql_mode);
  static int print_expr_to_default_value(ObRawExpr &expr,
                                         share::schema::ObColumnSchemaV2 &column,
                                         ObSchemaChecker *schema_checker,
                                         const common::ObTimeZoneInfo *tz_info);
  static int init_empty_session(const common::ObTimeZoneInfoWrap &tz_info_wrap,
                                const ObString *nls_formats,
                                const ObLocalSessionVar *local_session_var,
                                common::ObIAllocator &allocator,
                                share::schema::ObTableSchema &table_schema,
                                const ObSQLMode sql_mode,
                                ObSchemaChecker *schema_checker,
                                ObSQLSessionInfo &session_info);
  static int reformat_generated_column_expr(ObObj &default_value,
                                            const common::ObTimeZoneInfoWrap &tz_info_wrap,
                                            const common::ObString *nls_formats,
                                            const ObLocalSessionVar &local_session_var,
                                            common::ObIAllocator &allocator,
                                            share::schema::ObTableSchema &table_schema,
                                            share::schema::ObColumnSchemaV2 &column,
                                            const ObSQLMode sql_mode,
                                            ObSchemaChecker *schema_checker);
  static int resolve_generated_column_expr(
      ObString &expr_str,
      common::ObIAllocator &allocator,
      share::schema::ObTableSchema &table_schema,
      ObIArray<share::schema::ObColumnSchemaV2 *> &resolved_cols,
      share::schema::ObColumnSchemaV2 &column,
      ObSQLSessionInfo *session_info,
      ObSchemaChecker *schema_checker,
      ObRawExpr *&expr,
      ObRawExprFactory &expr_factory,
      bool coltype_not_defined = false);
  static int check_default_value(
      common::ObObj &default_value,
      const common::ObTimeZoneInfoWrap &tz_info_wrap,
      const common::ObString *nls_formats,
      const ObLocalSessionVar *local_session_var,
      common::ObIAllocator &allocator,
      share::schema::ObTableSchema &table_schema,
      share::schema::ObColumnSchemaV2 &column_schema,
      ObIArray<ObString> &gen_col_expr_arr,
      const ObSQLMode sql_mode,
      ObSchemaChecker *schema_checker,
      share::schema::ObColumnSchemaV2 *hidden_col = NULL);
  static int check_default_value(
      common::ObObj &default_value,
      const common::ObTimeZoneInfoWrap &tz_info_wrap,
      const common::ObString *nls_formats,
      const ObLocalSessionVar *local_session_var,
      common::ObIAllocator &allocator,
      share::schema::ObTableSchema &table_schema,
      ObIArray<share::schema::ObColumnSchemaV2> &resolved_cols,
      share::schema::ObColumnSchemaV2 &column_schema,
      ObIArray<ObString> &gen_col_expr_arr,
      const ObSQLMode sql_mode,
      ObSQLSessionInfo *session_info,
      ObSchemaChecker *schema_checker = NULL,
      bool coltype_not_defined = false);
  static int check_default_value(
      common::ObObj &default_value,
      const common::ObTimeZoneInfoWrap &tz_info_wrap,
      const common::ObString *nls_formats,
      common::ObIAllocator &allocator,
      share::schema::ObTableSchema &table_schema,
      ObIArray<share::schema::ObColumnSchemaV2 *> &resolved_cols,
      share::schema::ObColumnSchemaV2 &column_schema,
      ObIArray<ObString> &gen_col_expr_arr,
      const ObSQLMode sql_mode,
      ObSQLSessionInfo *session_info,
      ObSchemaChecker *schema_checker,
      bool coltype_not_defined = false);
  static int calc_default_value(
      share::schema::ObColumnSchemaV2 &column_schema,
      common::ObObj &default_value,
      const common::ObTimeZoneInfoWrap &tz_info_wrap,
      const common::ObString *nls_formats,
      common::ObIAllocator &allocator);
  static int check_udt_default_value(ObObj &default_value,
                                     const common::ObTimeZoneInfoWrap &tz_info_wrap,
                                     const common::ObString *nls_formats,
                                     ObIAllocator &allocator,
                                     ObTableSchema &table_schema,
                                     ObColumnSchemaV2 &column,
                                     const ObSQLMode sql_mode,
                                     ObSQLSessionInfo *session_info,
                                     ObSchemaChecker *schema_checker,
                                     obcall::ObDDLArg &ddl_arg);
  static int get_udt_column_default_values(const ObObj &default_value,
                                           const common::ObTimeZoneInfoWrap &tz_info_wrap,
                                           ObIAllocator &allocator,
                                           ObColumnSchemaV2 &column,
                                           const ObSQLMode sql_mode,
                                           ObSQLSessionInfo *session_info,
                                           ObSchemaChecker *schema_checker,
                                           ObObj &extend_result,
                                           obcall::ObDDLArg &ddl_arg);
  static int ob_add_ddl_dependency(const uint64_t schema_id,
                                   const ObSchemaType schema_type,
                                   const int64_t schema_version,
                                   obcall::ObDDLArg &ddl_arg);
  static int ob_add_ddl_dependency(const pl::ObPLDependencyTable & dependency_table,
                                   obcall::ObDDLArg &ddl_arg);
  static int add_udt_default_dependency(ObRawExpr *expr,
                                        ObSchemaChecker *schema_checker,
                                        obcall::ObDDLArg &ddl_arg);
  static int adjust_string_column_length_within_max(
      share::schema::ObColumnSchemaV2 &column);
  static int adjust_number_decimal_column_accuracy_within_max(share::schema::ObColumnSchemaV2 &column);
  static int fill_column_with_subschema(const ObRawExpr &expr,
                                        sql::ObSQLSessionInfo &session_info,
                                        share::schema::ObColumnSchemaV2 &column);

  // { used for enum and set
  int fill_extended_type_info(
      const ParseNode &str_list_node,
      share::schema::ObColumnSchemaV2 &column);
  int check_extended_type_info(
      share::schema::ObColumnSchemaV2 &column,
      ObSQLMode sql_mode);
  static int calc_enum_or_set_data_length(
      const ObIArray<common::ObString> &type_info,
      const ObCollationType &collation_type,
      const ObObjType &type,
      int32_t &length);
  static int calc_enum_or_set_data_length(
      share::schema::ObColumnSchemaV2 &column);
  static int check_type_info_incremental_change(
      const share::schema::ObColumnSchemaV2 &ori_schema,
      const share::schema::ObColumnSchemaV2 &new_schema,
      bool &is_incremental);
  static int cast_enum_or_set_default_value(
      const share::schema::ObColumnSchemaV2 &column,
      common::ObObjCastParams &params, common::ObObj &def_val);
  int check_partition_name_duplicate(ParseNode *node);
  static int check_text_column_length_and_promote(share::schema::ObColumnSchemaV2 &column,
                                                  int64_t table_id,
                                                  const bool is_byte_length = false);
  // this func is for compatibility, the row_store_type of OB_STORE_FORMAT_COMPRESSED_MYSQL
  static int get_row_store_type(const ObStoreFormatType store_format, ObRowStoreType &row_store_type);

  typedef common::hash::ObPlacementHashSet<share::schema::ObIndexNameHashWrapper, common::OB_MAX_COLUMN_NUMBER> IndexNameSet;
  int generate_index_name(ObString &index_name, IndexNameSet &current_index_name_set, const common::ObString &first_col_name);
  int resolve_range_partition_elements(ParseNode *node,
                                       const bool is_subpartition,
                                       const share::schema::ObPartitionFuncType part_type,
                                       const int64_t expr_num,
                                       common::ObIArray<ObRawExpr *> &range_value_exprs,
                                       common::ObIArray<share::schema::ObPartition> &partitions,
                                       common::ObIArray<share::schema::ObSubPartition> &subpartitions);
   int resolve_partition_hash_or_key(
       ObPartitionedStmt *stmt,
       ParseNode *node,
       const bool is_subpartition,
       share::schema::ObTableSchema &table_schema);
  int resolve_list_partition_elements(ParseNode *node,
                                      const bool is_subpartition,
                                      const share::schema::ObPartitionFuncType part_type,
                                      int64_t &expr_num,
                                      ObDDLStmt::array_t &list_value_exprs,
                                      common::ObIArray<share::schema::ObPartition> &partitions,
                                      common::ObIArray<share::schema::ObSubPartition> &subpartitions,
                                      const bool &in_tablegroup = false);
  //}
  int check_column_in_foreign_key(const share::schema::ObTableSchema &table_schema,
                                  const common::ObString &column_name,
                                  const bool is_drop_column);
  int check_is_json_contraint(const share::schema::ObTableSchema &tmp_table_schema,
                              ObIArray<ObConstraint> &csts,
                              ParseNode *cst_check_expr_node);

  int check_column_in_check_constraint(
      const share::schema::ObTableSchema &table_schema,
      const ObReducedVisibleColSet &drop_column_names_set,
      ObAlterTableStmt *alter_table_stmt);

  int check_index_columns_equal_foreign_key(const share::schema::ObTableSchema &table_schema,
                                            const share::schema::ObTableSchema &index_table_schema);
  static bool is_ids_match(const common::ObIArray<uint64_t> &src_list, const common::ObIArray<uint64_t> &dest_list);
  static int check_indexes_on_same_cols(const share::schema::ObTableSchema &table_schema,
                                        const share::schema::ObTableSchema &index_table_schema,
                                        ObSchemaChecker &schema_checker,
                                        bool &has_other_indexes_on_same_cols);
  static int resolve_check_constraint_expr(
        ObResolverParams &params,
        const ParseNode *node,
        const share::schema::ObTableSchema &table_schema,
        share::schema::ObConstraint &constraint,
        ObRawExpr *&check_expr,
        const share::schema::ObColumnSchemaV2 *column_schema = NULL);

  int resolve_partition_node(ObPartitionedStmt *stmt,
                             ParseNode *part_node,
                             share::schema::ObTableSchema &table_schema);
  int resolve_subpartition_option(ObPartitionedStmt *stmt,
                                  ParseNode *subpart_node,
                                  share::schema::ObTableSchema &table_schema);
  // @param [in] resolved_cols the columns which have been resolved in alter table, default null
  int resolve_spatial_index_constraint(
      const share::schema::ObTableSchema &table_schema,
      const common::ObString &column_name,
      int64_t column_num,
      const int64_t index_keyname_value,
      bool is_explicit_order,
      bool is_func_index,
      ObIArray<share::schema::ObColumnSchemaV2*> *resolved_cols = NULL,
      bool is_prefix_index = false);
  int resolve_spatial_index_constraint(
      const share::schema::ObColumnSchemaV2 &column_schema,
      int64_t column_num,
      const int64_t index_keyname_value,
      bool is_explicit_order,
      bool is_prefix_index = false);
  int resolve_fts_index_constraint(
      const share::schema::ObTableSchema &table_schema,
      const common::ObString &column_name,
      const int64_t index_keyname_value);
  int resolve_fts_index_constraint(
      const share::schema::ObColumnSchemaV2 &column_schema,
      const int64_t index_keyname_value);
  int resolve_multivalue_index_constraint(
      const share::schema::ObColumnSchemaV2 &column_schema,
      const int64_t index_keyname_value);
  int resolve_vec_index_constraint(
      const share::schema::ObTableSchema &table_schema,
      ObSchemaChecker &schema_checker,
      const common::ObString &column_name,
      const int64_t index_keyname_value,
      ParseNode *node);
  int resolve_vec_index_constraint(
      const share::schema::ObColumnSchemaV2 &column_schema,
      const int64_t index_keyname_value,
      ParseNode *node);
  static int get_partition_keys_by_part_func_expr(
      const ObString &part_func_expr_str,
      ObIAllocator &allocator,
      ObIArray<ObString> &partkey_strs);
protected:
  static int append_vec_hnsw_args(
      const ObPartitionResolveResult &resolve_result,
      const obcall::ObCreateIndexArg &index_arg,
      bool &vec_common_aux_table_exist,
      ObIArray<ObPartitionResolveResult> &resolve_results,
      ObIArray<ObCreateIndexArg> &index_arg_list,
      ObIAllocator *allocator,
      const ObSQLSessionInfo *session_info);

  static int append_vec_ivfflat_args(
      const ObPartitionResolveResult &resolve_result,
      const obcall::ObCreateIndexArg &index_arg,
      ObIArray<ObPartitionResolveResult> &resolve_results,
      ObIArray<ObCreateIndexArg> &index_arg_list,
      ObIAllocator *allocator);

  static int append_vec_ivfsq8_args(
      const ObPartitionResolveResult &resolve_result,
      const obcall::ObCreateIndexArg &index_arg,
      ObIArray<ObPartitionResolveResult> &resolve_results,
      ObIArray<ObCreateIndexArg> &index_arg_list,
      ObIAllocator *allocator);

  static int append_vec_ivfpq_args(
      const ObPartitionResolveResult &resolve_result,
      const obcall::ObCreateIndexArg &index_arg,
      ObIArray<ObPartitionResolveResult> &resolve_results,
      ObIArray<ObCreateIndexArg> &index_arg_list,
      ObIAllocator *allocator);

  static int get_part_str_with_type(
      share::schema::ObPartitionFuncType part_func_type,
      common::ObString &func_str,
      common::ObSqlString &part_str);
  int resolve_hints(const ParseNode *parse_node, ObDDLStmt &stmt, const ObTableSchema &table_schema);
  int calc_ddl_parallelism(const uint64_t hint_parallelism, const uint64_t table_dop, uint64_t &parallelism);
  int deep_copy_str(const common::ObString &src, common::ObString &dest);
  int set_vec_column_name(
      const common::ObString &column_name);
  int set_table_name(
      const common::ObString &table_name);
  int set_database_name(
      const common::ObString &database_name);
  int resolve_table_options(
      ParseNode *node,
      bool is_index_option);
  int resolve_table_option(
      const ParseNode *node,
      const bool is_index_option);
  int resolve_column_definition_ref(
      share::schema::ObColumnSchemaV2 &column,
      ParseNode *node,
      bool is_resolve_for_alter_table);
  int resolve_column_name(common::ObString &col_name, ParseNode *node);
  int resolve_column_name(share::schema::ObColumnSchemaV2 &column, ParseNode *node);
  int resolve_column_definition(
      share::schema::ObColumnSchemaV2 &column,
      ParseNode *node,
      ObColumnResolveStat &reslove_stat,
      bool &is_modify_column_visibility,
      common::ObString &pk_name,
      const ObTableSchema &table_schema,
      const bool unused_flag = false,
      const bool is_create_table_as = false,
      const bool allow_has_default = true);
  int drop_not_null_constraint(const share::schema::ObColumnSchemaV2 &column);
  int resolve_normal_column_attribute_constr_not_null(ObColumnSchemaV2 &column,
                                                      ParseNode *attrs_node,
                                                      ObColumnResolveStat &resolve_stat);
  int resolve_normal_column_attribute_constr_default(ObColumnSchemaV2 &column,
                                                     ParseNode *attr_node,
                                                     ObColumnResolveStat &resolve_stat,
                                                     ObObjParam& default_value,
                                                     bool& is_set_cur_default);
  int resolve_normal_column_attribute_constr_null(ObColumnSchemaV2 &column,
                                                  ObColumnResolveStat &resolve_stat);
  int resolve_normal_column_attribute(share::schema::ObColumnSchemaV2 &column,
                                      ParseNode *attrs_node,
                                      ObColumnResolveStat &reslove_stat,
                                      common::ObString &pk_name,
                                      bool &is_modify_column,
                                      bool &is_modify_column_visibility,
                                      const bool allow_has_default = true);
  int resolve_normal_column_attribute_check_cons(ObColumnSchemaV2 &column,
                                                 ParseNode *attrs_node,
                                                 ObCreateTableStmt *create_table_stmt);
  int resolve_normal_column_attribute_foreign_key(ObColumnSchemaV2 &column,
                                                  ParseNode *attrs_node,
                                                  ObCreateTableStmt *create_table_stmt);
  int resolve_generated_column_attribute(share::schema::ObColumnSchemaV2 &column,
                                         ParseNode *attrs_node,
                                         ObColumnResolveStat &reslove_stat);
  int resolve_srid_node(share::schema::ObColumnSchemaV2 &column,
                        const ParseNode &srid_node);
  int resolve_column_skip_index(
      const ParseNode &skip_index_node,
      share::schema::ObColumnSchemaV2 &column_schema);
  int check_skip_index(share::schema::ObTableSchema &table_schema);
  int resolve_lob_inrow_threshold(const ParseNode *option_node, const bool is_index_option);

  int resolve_lob_storage_parameters(const ParseNode *node);
  int resolve_lob_storage_parameter(share::schema::ObColumnSchemaV2 &column, const ParseNode &param_node);
  int resolve_lob_chunk_size(const ParseNode &size_node, int64_t &lob_chunk_size);
  int resolve_lob_chunk_size(share::schema::ObColumnSchemaV2 &column, const ParseNode &lob_chunk_size_node);
  int resolve_semistruct_encoding_type(const ParseNode *option_node, const bool is_index_option);
  /*
  int resolve_generated_column_definition(
      share::schema::ObColumnSchemaV2 &column,
      ParseNode *node,
      ObColumnResolveStat &reslove_stat);
  */

  virtual int add_storing_column(
      const common::ObString &column_name,
      bool check_column_exist = true,
      bool is_hidden = false,
      bool *has_invalid_types = NULL);
  virtual int get_table_schema_for_check(const share::schema::ObTableSchema *&table_schema)
  {
    UNUSED(table_schema);
    return common::OB_SUCCESS;
  };
  int fill_column_collation_info(
      const int64_t database_id,
      const common::ObString &table_name);
  int check_column_name_duplicate(
      const ParseNode *node);
  int resolve_partition_range(
      ObPartitionedStmt *stmt,
      ParseNode *node,
      const bool is_subpartition,
      share::schema::ObTableSchema &table_schema);
  int resolve_interval_clause(
      ObPartitionedStmt *stmt,
      ParseNode *node,
      share::schema::ObTableSchema &table_schema,
      common::ObSEArray<ObRawExpr*, 8> &range_exprs);
  static int resolve_interval_node(
      ObResolverParams &params,
      ParseNode *interval_node,
      common::ColumnType &col_dt,
      int64_t precision,
      int64_t scale,
      ObRawExpr *&interval_value_expr_out);
  static int resolve_interval_expr_low(
      ObResolverParams &params,
      ParseNode *interval_node,
      const share::schema::ObTableSchema &table_schema,
      ObRawExpr *transition_expr,
      ObRawExpr *&interval_value);
  int resolve_partition_list(
      ObPartitionedStmt *stmt,
      ParseNode *node,
      const bool is_subpartition,
      share::schema::ObTableSchema &table_schema);
    static int resolve_part_func(
      ObResolverParams &params,
      const ParseNode *node,
      const share::schema::ObPartitionFuncType partition_func_type,
      const share::schema::ObTableSchema &table_schema,
      common::ObIArray<ObRawExpr *> &part_fun_expr,
      common::ObIArray<common::ObString> &partition_keys);
    int set_partition_option_to_schema(
      share::schema::ObTableSchema &table_schema);
  int build_partition_key_info(
      share::schema::ObTableSchema &table_schema,
      common::ObSEArray<ObString, 4> &partition_keys,
      const share::schema::ObPartitionFuncType &part_func_type);
  int set_partition_keys(
      share::schema::ObTableSchema &table_schema,
      common::ObIArray<common::ObString> &partition_keys,
      bool is_subpart);
    int resolve_enum_or_set_column(
      const ParseNode *type_node,
      share::schema::ObColumnSchemaV2 &column);
    int resolve_collection_column(
      const ParseNode *type_node,
      share::schema::ObColumnSchemaV2 &column);


  static int is_gen_col_with_udf(const ObTableSchema &table_schema,
                                 const ObRawExpr *col_expr,
                                 bool &res);

  int resolve_range_value_exprs(ParseNode *expr_list_node,
                                const share::schema::ObPartitionFuncType part_type,
                                const common::ObString &partition_name,
                                common::ObIArray<ObRawExpr *> &range_value_exprs);

  int resolve_index_partition_node(
      ParseNode *index_partition_node,
      ObCreateIndexStmt *crt_idx_stmt);
  int check_key_cover_partition_keys(
      const bool is_range_part,
      const common::ObPartitionKeyInfo &part_key_info,
      share::schema::ObTableSchema &index_schema);
  int check_key_cover_partition_column(
      ObCreateIndexStmt *crt_idx_stmt,
      share::schema::ObTableSchema &index_schema);
  int generate_global_index_schema(
      ObCreateIndexStmt *crt_idx_stmt);
  int do_generate_global_index_schema(
      obcall::ObCreateIndexArg &create_index_arg,
      share::schema::ObTableSchema &table_schema);
  int resolve_check_constraint_node(
      const ParseNode &cst_node,
      common::ObIArray<share::schema::ObConstraint> &csts,
      const share::schema::ObColumnSchemaV2 *column_schema = NULL);
  int resolve_check_cst_state_node_mysql(const ParseNode* cst_check_state_node,
      share::schema::ObConstraint& cst);
  int resolve_pk_constraint_node(const ParseNode &cst_node,
                                 common::ObString pk_name,
                                 common::ObSEArray<share::schema::ObConstraint, 4> &csts);
  int resolve_foreign_key(const ParseNode *node, common::ObArray<int> &node_position_list);
  int resolve_foreign_key_node(
      const ParseNode *node,
      obcall::ObCreateForeignKeyArg &arg,
      bool is_alter_table = false,
      const share::schema::ObColumnSchemaV2 *column = NULL);
  int resolve_foreign_key_columns(const ParseNode *node, common::ObIArray<common::ObString> &columns);
  int resolve_foreign_key_options(const ParseNode *node,
                                  share::schema::ObReferenceAction &update_action,
                                  share::schema::ObReferenceAction &delete_action);
  int resolve_foreign_key_name(const ParseNode *constraint_node,
                               common::ObString &foreign_key_name,
                               ObNameGeneratedType &name_generated_type);
  int check_foreign_key_reference(
      obcall::ObCreateForeignKeyArg &arg,
      bool is_alter_table = false,
      const share::schema::ObColumnSchemaV2 *column = NULL);
  int resolve_match_options(const ParseNode *match_options_node);
  int create_fk_cons_name_automatically(ObString &foreign_key_name);
  int resolve_not_null_constraint_node(share::schema::ObColumnSchemaV2 &column,
                                       const ParseNode *cst_node);
  static int add_not_null_constraint(share::schema::ObColumnSchemaV2 &column,
                              const common::ObString &cst_name,
                              bool is_sys_generate_name,
                              share::schema::ObConstraint &cst,
                              common::ObIAllocator &allocator,
                              ObStmt *stmt);
  int create_name_for_empty_partition(const bool is_subpartition,
                                      ObIArray<share::schema::ObPartition> &partitions,
                                      ObIArray<share::schema::ObSubPartition> &subpartitions);
  template <typename PARTITION>
  int create_name_for_empty_partition(ObIArray<PARTITION> &partitions);
  template <typename PARTITION>
  int check_partition_name_valid(const ObIArray<PARTITION> &partitions,
                                 const common::ObString &part_name_str,
                                 bool &is_valid);
  bool is_column_exists(ObIArray<share::schema::ObColumnNameWrapper> &sort_column_array,
                        share::schema::ObColumnNameWrapper &column_key,
                        bool check_prefix_len);

  inline bool is_hash_type_partition(ObItemType type) const
  {
    return T_HASH_PARTITION == type || T_KEY_PARTITION == type;
  }
  inline bool is_range_type_partition(ObItemType type) const
  {
    return T_RANGE_PARTITION == type || T_RANGE_COLUMNS_PARTITION == type;
  }
  inline bool is_list_type_partition(ObItemType type) const
  {
    return T_LIST_PARTITION == type || T_LIST_COLUMNS_PARTITION == type;
  }

  int resolve_individual_subpartition(ObPartitionedStmt *stmt,
                                      ParseNode *part_node,
                                      ParseNode *partition_list_node,
                                      ParseNode *subpart_node,
                                      share::schema::ObTableSchema &table_schema,
                                      bool &force_template);

  int resolve_subpartition_elements(ObPartitionedStmt *stmt,
                                    ParseNode *node,
                                    share::schema::ObTableSchema &table_schema,
                                    share::schema::ObPartition *partition);

  int resolve_partition_name(ParseNode *partition_name_node,
                             ObString &partition_name,
                             share::schema::ObBasePartition &partition);

  int resolve_hash_or_key_partition_basic_infos(ParseNode *node,
                                                bool is_subpartition,
                                                share::schema::ObTableSchema &table_schema,
                                                share::schema::ObPartitionFuncType &part_func_type,
                                                ObString &func_expr_name);

  int resolve_range_partition_basic_infos(ParseNode *node,
                                          bool is_subpartition,
                                          share::schema::ObTableSchema &table_schema,
                                          share::schema::ObPartitionFuncType &part_func_type,
                                          ObString &func_expr_name,
                                          ObIArray<ObRawExpr*> &part_func_exprs);

  int resolve_list_partition_basic_infos(ParseNode *node,
                                         bool is_subpartition,
                                         share::schema::ObTableSchema &table_schema,
                                         share::schema::ObPartitionFuncType &part_func_type,
                                         ObString &func_expr_name,
                                         ObIArray<ObRawExpr*> &part_func_exprs);

  int resolve_hash_partition_elements(ObPartitionedStmt *stmt,
                                      ParseNode *node,
                                      share::schema::ObTableSchema &table_schema);

  int resolve_hash_subpartition_elements(ObPartitionedStmt *stmt,
                                         ParseNode *node,
                                         share::schema::ObTableSchema &table_schema,
                                         share::schema::ObPartition *partition);

  int resolve_range_partition_elements(ObPartitionedStmt *stmt,
                                       ParseNode *node,
                                       share::schema::ObTableSchema &table_schema,
                                       const share::schema::ObPartitionFuncType part_type,
                                       const ObIArray<ObRawExpr *> &part_func_exprs,
                                       ObIArray<ObRawExpr *> &range_value_exprs);

  int resolve_range_subpartition_elements(ObPartitionedStmt *stmt,
                                          ParseNode *node,
                                          share::schema::ObTableSchema &table_schema,
                                          share::schema::ObPartition *partition,
                                          const share::schema::ObPartitionFuncType part_type,
                                          const ObIArray<ObRawExpr *> &part_func_exprs,
                                          ObIArray<ObRawExpr *> &range_value_exprs);

  int resolve_list_partition_elements(ObPartitionedStmt*stmt,
                                      ParseNode *node,
                                      share::schema::ObTableSchema &table_schema,
                                      const share::schema::ObPartitionFuncType part_type,
                                      const ObIArray<ObRawExpr *> &part_func_exprs,
                                      ObDDLStmt::array_t &list_value_exprs);

  int resolve_list_subpartition_elements(ObPartitionedStmt *stmt,
                                         ParseNode *node,
                                         share::schema::ObTableSchema &table_schema,
                                         share::schema::ObPartition *partition,
                                         const share::schema::ObPartitionFuncType part_type,
                                         const ObIArray<ObRawExpr *> &part_func_exprs,
                                         ObDDLStmt::array_t &list_value_exprs);

  int resolve_range_partition_value_node(ParseNode &expr_list_node,
                                         const ObString &partition_name,
                                         const share::schema::ObPartitionFuncType part_type,
                                         const ObIArray<ObRawExpr *> &part_func_exprs,
                                         ObIArray<ObRawExpr *> &range_value_exprs);

  int resolve_list_partition_value_node(ParseNode &expr_list_node,
                                        const ObString &partition_name,
                                        const share::schema::ObPartitionFuncType part_type,
                                        const ObIArray<ObRawExpr *> &part_func_exprs,
                                        ObDDLStmt::array_t &list_value_exprs);

  int generate_default_hash_part(const int64_t partition_num,
                                 share::schema::ObTableSchema &table_schema);

  int generate_default_hash_subpart(ObPartitionedStmt *stmt,
                                    const int64_t partition_num,
                                    share::schema::ObTableSchema &table_schema,
                                    share::schema::ObPartition *partition);

  int generate_default_range_subpart(ObPartitionedStmt *stmt,
                                     ObTableSchema &table_schema,
                                     ObPartition *partition,
                                     ObIArray<ObRawExpr *> &range_value_exprs);

  int generate_default_list_subpart(ObPartitionedStmt *stmt,
                                    ObTableSchema &table_schema,
                                    ObPartition *partition,
                                    ObIArray<ObRawExpr *> &list_value_exprs);

  int check_and_set_partition_names(ObPartitionedStmt *stmt,
                                    share::schema::ObTableSchema &table_schema);
  int check_and_set_partition_names(ObPartitionedStmt *stmt,
                                    share::schema::ObTableSchema &table_schema,
                                    bool is_subpart);
  int check_and_set_individual_subpartition_names(ObPartitionedStmt *stmt,
                                                  share::schema::ObTableSchema &table_schema);
  int set_partition_name_in_hashset(const share::schema::ObPartitionNameHashWrapper &partition_name_key, 
                                    ObPartitionNameSet &partition_name_set);
  int deep_copy_string_in_part_expr(ObPartitionedStmt* stmt);
  int deep_copy_column_expr_name(common::ObIAllocator &allocator, ObIArray<ObRawExpr*> &exprs);
  int check_index_param(const ParseNode *option_node, ObString &index_params, const int64_t vector_dim);


  void reset();

  // for alter table: there may be some index_arg.
  // while create table can garentee some table info is behind the index arg, which will not be
  // reset
  void reset_index();
  static int trim_space_for_default_value(
      const bool is_mysql_mode,
      const bool is_char_type,
      const ObCollationType &collation_type,
      ObObj &default_value, ObString &str);
  bool is_organization_set_to_heap() { return table_organization_ == ObTableOrganizationType::OB_HEAP_ORGANIZATION; }
  int64_t block_size_;
  int64_t consistency_level_;
  INDEX_TYPE index_scope_;
  int64_t tablet_size_;
  int64_t pctfree_;
  uint64_t index_attributes_set_;
  common::ObCharsetType charset_type_;
  common::ObCollationType collation_type_;
  common::ObString compress_method_;
  common::ObString parser_name_;
  common::ObString parser_properties_;
  common::ObString comment_;
  common::ObRowStoreType row_store_type_;
  common::ObStoreFormatType store_format_;
  int64_t progressive_merge_num_;
  bool read_only_;
  bool with_rowid_;
  common::ObString table_name_;
  common::ObString database_name_;
  share::schema::ObPartitionFuncType partition_func_type_;
  common::ObString partition_expr_;
  uint64_t auto_increment_;
  common::ObString index_name_;
  INDEX_KEYNAME index_keyname_;
  bool global_;
  common::ObArray<common::ObString> store_column_names_;
  common::ObArray<common::ObString> hidden_store_column_names_;
  common::ObSEArray<share::schema::ObColumnNameWrapper, 16, common::ModulePageAllocator, true> sort_column_array_;
  common::hash::ObPlacementHashSet<share::schema::ObColumnNameHashWrapper,
                                   common::OB_MAX_COLUMN_NUMBER> storing_column_set_;
  common::hash::ObPlacementHashSet<share::schema::ObForeignKeyNameHashWrapper,
                                   OB_MAX_AUX_TABLE_PER_MAIN_TABLE> current_foreign_key_name_set_;
  common::ObBitSet<> alter_table_bitset_;
  bool has_index_using_type_;
  share::schema::ObIndexUsingType index_using_type_;
  bool enable_row_movement_;
  share::schema::ObTableMode table_mode_;
  int64_t table_dop_; // default value is 1
  int64_t hash_subpart_num_;
  ObNameGeneratedType name_generated_type_;
  bool have_generate_fts_arg_;
  bool is_set_lob_inrow_threshold_;
  int64_t lob_inrow_threshold_;
  bool have_generate_vec_arg_;
  int64_t auto_increment_cache_size_;
  common::ObString index_params_;
  ObTableOrganizationType table_organization_;
  common::ObString vec_column_name_;
  ObIndexType vec_index_type_;
  ObSemiStructEncodingType semistruct_encoding_type_;
private:
  template <typename STMT>
  DISALLOW_COPY_AND_ASSIGN(ObDDLResolver);
};
//FIXME:support non-template secondary partitioning

/**
 * @brief create_name_for_empty_partition
 * Automatically name partitions that the user has not explicitly named, with partition names Pnumber
 * number starts from 8192 and increments
 */
template <typename PARTITION>
int ObDDLResolver::create_name_for_empty_partition(ObIArray<PARTITION> &partitions)
{
  int ret = OB_SUCCESS;
  int64_t max_part_id = OB_MAX_PARTITION_NUM_MYSQL;
  common::ObString part_name_str;
  for (int64_t i = 0; OB_SUCC(ret) && i < partitions.count(); ++i) {
    PARTITION &part = partitions.at(i);
    bool is_valid = !part.is_empty_partition_name();
    while (!is_valid) {
      char part_name[OB_MAX_PARTITION_NAME_LENGTH];
      int64_t pos = 0;
      if (OB_FAIL(databuff_printf(part_name, OB_MAX_PARTITION_NAME_LENGTH,
          pos, "P%ld", max_part_id))) {
        SQL_RESV_LOG(WARN, "failed to print databuff", K(ret), K(max_part_id));
      } else if (FALSE_IT(part_name_str.assign(part_name, static_cast<int32_t>(pos)))) {
        // never reach
      } else if (OB_FAIL(check_partition_name_valid(partitions, part_name_str, is_valid))) {
        SQL_RESV_LOG(WARN, "failed to check partition name valid", K(ret), K(part_name_str));
      } else if (is_valid) {
        if (OB_FAIL(part.set_part_name(part_name_str))) {
          SQL_RESV_LOG(WARN, "failed to set partition name", K(ret), K(part_name_str));
        } else {
          part.set_is_empty_partition_name(false);
          ++max_part_id;
        }
      } else {
        ++max_part_id;
      }
    }
  }
  return ret;
}

template <typename PARTITION>
int ObDDLResolver::check_partition_name_valid(const ObIArray<PARTITION> &partitions,
                                              const common::ObString &part_name_str,
                                              bool &is_valid)
{
  int ret = OB_SUCCESS;
  is_valid = true;
  for (int64_t i = 0; OB_SUCC(ret) && i < partitions.count(); ++i) {
    const PARTITION &part = partitions.at(i);
    if (part.is_empty_partition_name()) {
      // do nothing
    } else if (common::ObCharset::case_insensitive_equal(part.get_part_name(),
                                                         part_name_str)) {
      is_valid = false;
    }
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SQL_RESOLVER_DDL_OB_DDL_RESOLVER_H_ */
