/*
 * Copyright (c) 2026 OceanBase.
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
#pragma once
#include "mysqlclient/ob_mysql_transaction.h"
#include <new>

namespace oceanbase { namespace rootserver {

// Root-only transaction boundary shared by native GRANT and REVOKE. Production
// supplies ObDDLSQLTransaction, whose start fences the current schema/DDL stream
// and whose end prepares the schema commit. No borrowed caller transaction is
// accepted. apply writes the complete batch and reports its real maximum ACL
// version; publish is called only after a confirmed, changing commit.
template<class Apply, class Publish>
int execute_native_routine_privilege_transaction(common::ObMySQLTransaction &transaction,
    common::ObISQLClient &proxy, int64_t schema_version, Apply &&apply, Publish &&publish)
{
  using namespace common;
  if (transaction.is_started()) return OB_STATE_NOT_MATCH;
  if (schema_version <= 0) return OB_INVALID_ARGUMENT;
  int ret = OB_SUCCESS;
  int64_t changed_version = 0;
  try {
    ret = transaction.start(&proxy, schema_version, false);
    if (ret == OB_SUCCESS && !transaction.is_started()) ret = OB_STATE_NOT_MATCH;
    if (ret == OB_SUCCESS) ret = apply(changed_version);
    if (ret == OB_SUCCESS && !transaction.is_started()) ret = OB_STATE_NOT_MATCH;
    if (ret == OB_SUCCESS && changed_version < 0) ret = OB_ERR_UNEXPECTED;
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { ret = OB_ERR_UNEXPECTED; }
  if (transaction.is_started()) {
    const int end_ret = transaction.end(ret == OB_SUCCESS);
    if (ret == OB_SUCCESS) ret = end_ret;
  }
  // Never replay after a failed/unknown commit or disguise a publication error
  // as success. The primary write/admission failure wins over rollback failure.
  if (ret == OB_SUCCESS && changed_version > 0) ret = publish();
  return ret;
}
} }
