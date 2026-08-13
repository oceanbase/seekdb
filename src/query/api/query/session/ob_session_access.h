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

#ifndef OCEANBASE_QUERY_API_SESSION_OB_SESSION_ACCESS_H_
#define OCEANBASE_QUERY_API_SESSION_OB_SESSION_ACCESS_H_

#include <stdint.h>
#include "common/object/ob_object.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/ob_define.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace sql
{
class ObBasicSessionInfo;
class ObSQLSessionInfo;
}
namespace query
{

// Transitional session capability facade.  It deliberately exposes common
// values and opaque saved state, never ObSQLSessionInfo layout or nested types.
class ObSessionAccess
{
public:
  static int64_t get_query_timeout_ts(
      const sql::ObBasicSessionInfo *session);
  static int get_autocommit(
      const sql::ObSQLSessionInfo *session, bool &autocommit);
  static int set_autocommit(sql::ObSQLSessionInfo *session, bool autocommit);
  static bool is_inner(const sql::ObSQLSessionInfo *session);
  static bool is_in_transaction(const sql::ObSQLSessionInfo *session);
  static void set_inner_session(sql::ObSQLSessionInfo *session);
  static void set_user_session(sql::ObSQLSessionInfo *session);

  static common::ObString get_database_name(
      const sql::ObSQLSessionInfo *session);
  static uint64_t get_database_id(const sql::ObSQLSessionInfo *session);
  static void set_database_id(sql::ObSQLSessionInfo *session, uint64_t id);
  static int set_default_database(
      sql::ObSQLSessionInfo *session, const common::ObString &name);

  static int get_collation_connection(
      const sql::ObSQLSessionInfo *session, common::ObObj &value);
  static int set_collation_connection(
      sql::ObSQLSessionInfo *session, const common::ObObj &value);

  static void set_dummy_ddl_visibility(
      sql::ObSQLSessionInfo *session, bool enabled);

  static int get_name_case_mode(
      const sql::ObSQLSessionInfo *session,
      common::ObNameCaseMode &case_mode);
  static int get_connection_collation(
      const sql::ObSQLSessionInfo *session,
      common::ObCollationType &collation);
  static uint32_t get_server_session_id(
      const sql::ObSQLSessionInfo *session);
  static void *get_btree_iter_cache(sql::ObSQLSessionInfo *session);
  static void get_current_sql_id(
      const sql::ObSQLSessionInfo *session,
      char *buffer,
      int64_t buffer_size);
  static void set_query_command(sql::ObSQLSessionInfo *session);
  static int get_force_parallel_dml_dop(
      const sql::ObSQLSessionInfo *session,
      uint64_t &dop);

  static int save_statement_state(
      sql::ObSQLSessionInfo *session,
      common::ObIAllocator &allocator,
      void *&saved_state);
  static int restore_statement_state(
      sql::ObSQLSessionInfo *session,
      common::ObIAllocator &allocator,
      void *&saved_state);
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_SESSION_OB_SESSION_ACCESS_H_
