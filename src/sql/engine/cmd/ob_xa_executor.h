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

#ifndef OCEANBASE_SQL_ENGINE_CMD_OB_XA_CMD_EXECUTOR_
#define OCEANBASE_SQL_ENGINE_CMD_OB_XA_CMD_EXECUTOR_

#include "lib/utility/ob_macro_utils.h"
#include "lib/string/ob_string.h"
#include "sql/resolver/xa/ob_xa_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObXaStartStmt;
class ObXaExecutorUtil
{
public:
  static int get_org_cluster_id(ObSQLSessionInfo *session, int64_t &org_cluster_id);
};

class ObXaStartExecutor
{
public:
  ObXaStartExecutor() {}
  ~ObXaStartExecutor() {}
  int execute(ObExecContext &ctx, ObXaStartStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaStartExecutor);
};

class ObXaEndStmt;
class ObXaEndExecutor
{
public:
  ObXaEndExecutor() {}
  ~ObXaEndExecutor() {}
  int execute(ObExecContext &ctx, ObXaEndStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaEndExecutor);
};

class ObXaPrepareStmt;
class ObXaPrepareExecutor
{
public:
  ObXaPrepareExecutor() {}
  ~ObXaPrepareExecutor() {}
  int execute(ObExecContext &ctx, ObXaPrepareStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaPrepareExecutor);
};

class ObXaCommitStmt;
class ObXaRollBackStmt;

class ObXaCommitExecutor
{
public:
  ObXaCommitExecutor() {}
  ~ObXaCommitExecutor() {}
  int execute(ObExecContext &ctx, ObXaCommitStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaCommitExecutor);
};

class ObXaRollbackExecutor
{
public:
  ObXaRollbackExecutor() {}
  ~ObXaRollbackExecutor() {}
  int execute(ObExecContext &ctx, ObXaRollBackStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObXaRollbackExecutor);
};

} // end namespace sql
} // end namespace oceanbase


#endif
