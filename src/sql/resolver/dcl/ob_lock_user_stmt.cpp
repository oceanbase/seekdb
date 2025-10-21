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

#include "sql/resolver/dcl/ob_lock_user_stmt.h"
using namespace oceanbase::common;
using namespace oceanbase::sql;
ObLockUserStmt::ObLockUserStmt(common::ObIAllocator *name_pool)
    : ObDDLStmt(name_pool, stmt::T_LOCK_USER), tenant_id_(OB_INVALID_ID) , locked_(false)
{
}

ObLockUserStmt::ObLockUserStmt()
    : ObDDLStmt(NULL, stmt::T_LOCK_USER), tenant_id_(OB_INVALID_ID) , locked_(false)
{
}

ObLockUserStmt::~ObLockUserStmt()
{
}

int ObLockUserStmt::add_user(const ObString &user_name, const common::ObString &host_name)
{
  int ret = OB_SUCCESS;
  if (0 == user_name.compare(OB_SYS_USER_NAME)
      && 0 == host_name.compare(OB_SYS_HOST_NAME)) {
    ret = OB_ERR_NO_PRIVILEGE;
    SQL_RESV_LOG(WARN, "Can not lock root user", K(ret));
  } else if (OB_FAIL(user_.add_string(user_name))) {
    SQL_RESV_LOG(WARN, "Add user failed", K(user_name), K(ret));
  } else if (OB_FAIL(user_.add_string(host_name))) {
    SQL_RESV_LOG(WARN, "Add host failed", K(user_name), K(host_name), K(ret));
  } else {
    //do nothing
  }
  return ret;
}

