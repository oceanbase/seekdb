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
#include "sql/resolver/dcl/ob_create_role_stmt.h"


using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

ObCreateRoleStmt::ObCreateRoleStmt(ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_CREATE_ROLE),
      tenant_id_(OB_INVALID_ID),
      role_name_(),
      password_(),
      need_enc_(false),
      if_not_exists_(false)
{
}

ObCreateRoleStmt::ObCreateRoleStmt()
    : ObDDLStmt(NULL, stmt::T_CREATE_ROLE),
      tenant_id_(OB_INVALID_ID),
      role_name_(),
      password_(),
      need_enc_(false),
      if_not_exists_(false)
{
}

ObCreateRoleStmt::~ObCreateRoleStmt()
{
}

int64_t ObCreateRoleStmt::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (NULL != buf) {
    J_OBJ_START();
    J_KV(N_STMT_TYPE, ((int)stmt_type_),
         "role_to_create", role_name_,
         "password", password_);
    J_OBJ_END();
  }
  return pos;
}
