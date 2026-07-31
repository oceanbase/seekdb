/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_ENGINE_CMD_OB_DDL_EXECUTION_H_
#define OCEANBASE_QUERY_API_ENGINE_CMD_OB_DDL_EXECUTION_H_

#include <stdint.h>

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace query
{

// Transitional public facade for query-owned DDL lifecycle behavior used by
// data-plane orchestration. The concrete SQL utility remains private.
class ObDDLExecution
{
public:
  static int wait_ddl_finish(
      const int64_t task_id,
      const bool ddl_need_retry_at_executor,
      sql::ObSQLSessionInfo *session,
      const bool is_support_cancel = true);
  static int handle_session_exception(sql::ObSQLSessionInfo &session);
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_CMD_OB_DDL_EXECUTION_H_
