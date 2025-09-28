/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/dcl/ob_grant_stmt.h"
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{
ObGrantStmt::ObGrantStmt(ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_GRANT),
      priv_set_(0),
      grant_level_(OB_PRIV_INVALID_LEVEL),
      database_(),
      table_(),
      tenant_id_(OB_INVALID_ID),
      masked_sql_(),
      need_create_user_(false),
      need_create_user_priv_(false),
      user_name_set_(),
      role_name_set_(),
      object_type_(share::schema::ObObjectType::INVALID),
      object_id_(OB_INVALID_ID),
      grantor_id_(OB_INVALID_ID),
      option_(0),
      sys_priv_array_(),
      obj_priv_array_(),
      sel_col_ids_(),
      ins_col_ids_(),
      upd_col_ids_(),
      ref_col_ids_(),
      ref_query_(NULL),
      is_grant_all_tab_priv_(false),
      table_schema_version_(0)
{
}

ObGrantStmt::ObGrantStmt()
    : ObDDLStmt(NULL, stmt::T_GRANT),
      priv_set_(0),
      grant_level_(OB_PRIV_INVALID_LEVEL),
      database_(),
      table_(),
      tenant_id_(OB_INVALID_ID),
      masked_sql_(),
      need_create_user_(false),
      need_create_user_priv_(false),
      user_name_set_(),
      role_name_set_(),
      object_type_(share::schema::ObObjectType::INVALID),
      object_id_(OB_INVALID_ID),
      grantor_id_(OB_INVALID_ID),
      option_(0),
      sys_priv_array_(),
      obj_priv_array_(),
      sel_col_ids_(),
      ins_col_ids_(),
      upd_col_ids_(),
      ref_col_ids_(),
      ref_query_(NULL),
      is_grant_all_tab_priv_(false),
      table_schema_version_(0)
{
}

ObGrantStmt::~ObGrantStmt()
{
}

int ObGrantStmt::add_user(const common::ObString &user_name,
                          const common::ObString &host_name,
                          const common::ObString &pwd,
                          const common::ObString &need_enc)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(users_.add_string(user_name))) {
    LOG_WARN("failed to add user", K(ret));
  } else if (OB_FAIL(users_.add_string(host_name))) {
    LOG_WARN("failed to add host_name", K(ret));
  } else if (OB_FAIL(users_.add_string(pwd))) {
    LOG_WARN("failed to add password", K(ret));
  } else if (OB_FAIL(users_.add_string(need_enc))) {
    LOG_WARN("failed to add need enc", K(ret));
  } else {
    //do nothing
  }
  return ret;
}


// Pust the first user_name and host_name into role[0] and role[1]
// And the rest of user_names and host_names shall be saved in remain_roles
// The remain_roles is used to keep compatibility with previous logic


void ObGrantStmt::set_grant_level(ObPrivLevel grant_level)
{
  grant_level_ = grant_level;
}

void ObGrantStmt::set_priv_set(ObPrivSet priv_set)
{
  priv_set_ = priv_set;
}

int ObGrantStmt::set_database_name(const ObString &database)
{
  int ret = OB_SUCCESS;
  database_ = database;
  return ret;
}

int ObGrantStmt::set_table_name(const ObString &table)
{
  int ret = OB_SUCCESS;
  table_ = table;
  return ret;
}



int ObGrantStmt::add_grantee(const ObString &grantee)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(grantees_.add_string(grantee))) {
    LOG_WARN("failed to add grantee", K(ret));
  }
  return ret;
}

int64_t ObGrantStmt::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (NULL != buf) {
    J_OBJ_START();
    J_KV(N_STMT_TYPE, ((int)stmt_type_),
         "priv_set", share::schema::ObPrintPrivSet(priv_set_),
         "grant_level", ob_priv_level_str(grant_level_),
         "database", database_,
         "table", table_,
         "users", users_,
         "object_type", object_type_,
         "object_id", object_id_);
    J_OBJ_END();
  }
  return pos;
}

}
}


