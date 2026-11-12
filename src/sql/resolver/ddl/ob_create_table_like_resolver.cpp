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

#include "sql/resolver/ddl/ob_create_table_like_resolver.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObCreateTableLikeResolver::ObCreateTableLikeResolver(ObResolverParams &params)
    : ObDDLResolver(params)
{
}

ObCreateTableLikeResolver::~ObCreateTableLikeResolver()
{
}

int ObCreateTableLikeResolver::resolve(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "session info should not be null", K(ret));
  } else if (peg::NodeKind::CreateTableLike != parse_tree.kind()) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "invalid parse tree", K(ret));
  } else {
    ObCreateTableLikeStmt *create_table_like_stmt = NULL;
    if (NULL == (create_table_like_stmt = create_stmt<ObCreateTableLikeStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "failed to create create_table_like stmt", K(ret));
    } else {
      stmt_ = create_table_like_stmt;
    }
    if (OB_SUCC(ret)) {
      create_table_like_stmt->set_table_type(peg::table_like_temporary(parse_tree)
          ? share::schema::TMP_TABLE : share::schema::USER_TABLE);
      create_table_like_stmt->set_if_not_exist(peg::table_like_if_not_exists(parse_tree));
    }

    if (OB_SUCC(ret)) {
      const auto *new_relation_node = &peg::table_like_relation(parse_tree);
      const auto *origin_relation_node = &peg::table_like_source(parse_tree);
      if (NULL != new_relation_node && NULL != origin_relation_node) {
        //resolve table
        ObString origin_table_name;
        ObString origin_database_name;
        ObString new_table_name;
        ObString new_database_name;
        if (OB_FAIL(resolve_table_relation_node(new_relation_node,
                                                new_table_name,
                                                new_database_name))) {
        } else if (OB_FAIL(resolve_table_relation_node(origin_relation_node,
                                                       origin_table_name,
                                                       origin_database_name))) {
        } else if (ObString(OB_RECYCLEBIN_SCHEMA_NAME) == new_database_name
                   || ObString(OB_PUBLIC_SCHEMA_NAME) == new_database_name) {
          ret = OB_OP_NOT_ALLOW;
          SQL_RESV_LOG(WARN, "create table in recyclebin database is not allowed", K(ret),
                       K(new_table_name), K(new_database_name),
                       K(origin_table_name), K(origin_database_name));
        } else {
          bool db_equal = false;
          bool table_equal = false;
          if (OB_FAIL(ObResolverUtils::name_case_cmp(session_info_,
                                                     origin_database_name,
                                                     new_database_name,
                                                     OB_TABLE_NAME_CLASS,
                                                     db_equal))) {
          } else if (OB_FAIL(ObResolverUtils::name_case_cmp(session_info_,
                                                            origin_table_name,
                                                            new_table_name,
                                                            OB_TABLE_NAME_CLASS,
                                                            table_equal))) {
          } else if (db_equal && table_equal) {
            ret = OB_ERR_NONUNIQ_TABLE;
            LOG_USER_ERROR(OB_ERR_NONUNIQ_TABLE, origin_table_name.length(), origin_table_name.ptr());
          } else {
            create_table_like_stmt->set_new_table_name(new_table_name);
            create_table_like_stmt->set_new_db_name(new_database_name);
            create_table_like_stmt->set_origin_table_name(origin_table_name);
            create_table_like_stmt->set_origin_db_name(origin_database_name);
            
          }
        }
      } else {
        ret = OB_ERR_PARSE_SQL;
        SQL_RESV_LOG(WARN, "relation node should not be null!", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      create_table_like_stmt->set_define_user_id(session_info_->get_priv_user_id());
    }
  }
  return ret;
}

} //namespace common
} //namespace oceanbase
