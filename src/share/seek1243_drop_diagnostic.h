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

#pragma once

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
}

// This is intentionally source-local to the controller's isolated Windows
// diagnostic branch.  It is not a normal build or CI switch and must not be
// carried into a product fix.  Linux builds keep the diagnostic disabled.
#if defined(_WIN32) && !defined(OB_SEEK1243_DROP_DIAGNOSTIC)
#define OB_SEEK1243_DROP_DIAGNOSTIC 1
#endif

#ifdef OB_SEEK1243_DROP_DIAGNOSTIC
#include "common/mysqlclient/ob_isql_client.h"
#include "common/mysqlclient/ob_isql_connection.h"
#include "common/ob_timeout_ctx.h"
#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
namespace share
{
namespace seek1243
{

inline uint64_t current_trace_id()
{
  const uint64_t *trace_id = common::ObCurTraceId::get();
  return NULL == trace_id ? 0 : trace_id[0];
}

// The context belongs to the SQL connection, not to a worker thread.  This is
// intentional: inner SQL may cross coroutine/task scheduling boundaries, and
// a thread_local guard would mis-attribute a child query after migration.
class DropConnectionScope
{
public:
  DropConnectionScope(common::ObISQLClient &client,
                      const bool enabled,
                      const uint64_t table_id,
                      const int64_t table_type)
      : conn_(enabled ? client.get_connection() : NULL),
        saved_active_(false),
        saved_table_id_(0),
        saved_table_type_(0)
  {
    if (NULL != conn_) {
      uint64_t ignored_session_id = 0;
      int64_t ignored_inner_tx_id = 0;
      saved_active_ = conn_->get_seek1243_drop_context(
          saved_table_id_, saved_table_type_, ignored_session_id, ignored_inner_tx_id);
      conn_->set_seek1243_drop_context(table_id, table_type);
    }
  }

  ~DropConnectionScope()
  {
    if (NULL != conn_) {
      if (saved_active_) {
        conn_->clear_seek1243_drop_context();
        conn_->set_seek1243_drop_context(saved_table_id_, saved_table_type_);
      }
      // A fresh DROP context is kept on this connection until commit/rollback
      // so the transaction-end event remains attributable to the same DROP.
      // ObInnerSQLConnection clears it immediately after those boundaries.
    }
  }

private:
  common::sqlclient::ObISQLConnection *conn_;
  bool saved_active_;
  uint64_t saved_table_id_;
  int64_t saved_table_type_;
};

// These are bounded, end-only measurements at the selected DROP boundaries;
// they deliberately do not trace arbitrary SQL or data.  A parent boundary
// includes the wrapped child call and the child's final LOG_INFO.  A child
// stage's elapsed value ends before its own final LOG_INFO, so parent and child
// values overlap and must not be summed.  A call that is still in flight when
// the deadline expires has no completed stage record.
class DropStageScope
{
public:
  DropStageScope(const char *stage,
                 const uint64_t table_id,
                 const int64_t table_type,
                 int &ret)
      : stage_(stage), table_id_(table_id), table_type_(table_type), ret_(ret),
        wall_start_us_(common::ObTimeUtility::current_time()),
        monotonic_start_us_(common::ObTimeUtility::current_monotonic_raw_time()),
        deadline_us_(common::ObTimeoutCtx::get_ctx().is_timeout_set()
                         ? common::ObTimeoutCtx::get_ctx().get_abs_timeout() : 0),
        session_id_(0),
        inner_tx_id_(0)
  {}

  DropStageScope(common::ObISQLClient &client,
                 const char *stage,
                 const uint64_t table_id,
                 const int64_t table_type,
                 int &ret)
      : stage_(stage), table_id_(table_id), table_type_(table_type), ret_(ret),
        wall_start_us_(common::ObTimeUtility::current_time()),
        monotonic_start_us_(common::ObTimeUtility::current_monotonic_raw_time()),
        deadline_us_(common::ObTimeoutCtx::get_ctx().is_timeout_set()
                         ? common::ObTimeoutCtx::get_ctx().get_abs_timeout() : 0),
        session_id_(0),
        inner_tx_id_(0)
  {
    common::sqlclient::ObISQLConnection *conn = client.get_connection();
    if (NULL != conn) {
      uint64_t context_table_id = table_id_;
      int64_t context_table_type = table_type_;
      (void)conn->get_seek1243_drop_context(
          context_table_id, context_table_type, session_id_, inner_tx_id_);
    }
  }

  ~DropStageScope()
  {
    const int64_t monotonic_end_us = common::ObTimeUtility::current_monotonic_raw_time();
    const int64_t wall_end_us = common::ObTimeUtility::current_time();
    LOG_INFO("seek1243 drop diagnostic table boundary",
             K(stage_), "trace_id", current_trace_id(),
             K(table_id_), K(table_type_), K(session_id_), K(inner_tx_id_), K(ret_),
             K(deadline_us_), K(wall_start_us_), K(wall_end_us),
             "elapsed_us", monotonic_end_us - monotonic_start_us_);
  }

private:
  const char *stage_;
  const uint64_t table_id_;
  const int64_t table_type_;
  int &ret_;
  const int64_t wall_start_us_;
  const int64_t monotonic_start_us_;
  const int64_t deadline_us_;
  uint64_t session_id_;
  int64_t inner_tx_id_;
};

template <typename Fn>
int run_drop_stage(const char *stage,
                   const uint64_t table_id,
                   const int64_t table_type,
                   Fn &&fn)
{
  const int64_t wall_start_us = common::ObTimeUtility::current_time();
  const int64_t monotonic_start_us = common::ObTimeUtility::current_monotonic_raw_time();
  const common::ObTimeoutCtx &timeout_ctx = common::ObTimeoutCtx::get_ctx();
  const int64_t deadline_us = timeout_ctx.is_timeout_set() ? timeout_ctx.get_abs_timeout() : 0;
  const int ret = fn();
  const int64_t monotonic_end_us = common::ObTimeUtility::current_monotonic_raw_time();
  const int64_t wall_end_us = common::ObTimeUtility::current_time();
  LOG_INFO("seek1243 drop diagnostic stage",
           K(stage), K(table_id), K(table_type), K(ret),
           K(deadline_us), K(wall_start_us), K(wall_end_us),
           "elapsed_us", monotonic_end_us - monotonic_start_us);
  return ret;
}

template <typename Fn>
int run_drop_connection_stage(common::ObISQLClient &client,
                              const char *stage,
                              const uint64_t table_id,
                              const int64_t table_type,
                              Fn &&fn)
{
  uint64_t context_table_id = 0;
  int64_t context_table_type = 0;
  uint64_t session_id = 0;
  int64_t inner_tx_id = 0;
  common::sqlclient::ObISQLConnection *conn = client.get_connection();
  if (NULL != conn && conn->get_seek1243_drop_context(
          context_table_id, context_table_type, session_id, inner_tx_id)) {
  }
  const int64_t wall_start_us = common::ObTimeUtility::current_time();
  const int64_t monotonic_start_us = common::ObTimeUtility::current_monotonic_raw_time();
  const common::ObTimeoutCtx &timeout_ctx = common::ObTimeoutCtx::get_ctx();
  const int64_t deadline_us = timeout_ctx.is_timeout_set() ? timeout_ctx.get_abs_timeout() : 0;
  const int ret = fn();
  const int64_t monotonic_end_us = common::ObTimeUtility::current_monotonic_raw_time();
  const int64_t wall_end_us = common::ObTimeUtility::current_time();
  LOG_INFO("seek1243 drop diagnostic table boundary",
           K(stage), "trace_id", current_trace_id(),
           K(table_id), K(table_type), K(session_id), K(inner_tx_id), K(ret),
           K(deadline_us), K(wall_start_us), K(wall_end_us),
           "elapsed_us", monotonic_end_us - monotonic_start_us);
  return ret;
}

// The connection-aware form is used for metadata children and therefore can
// carry the connection's session/inner-transaction identity.  Its elapsed
// value is inclusive of nested logging and children; it is not an exclusive
// primitive cost and must not be added to child values.

} // namespace seek1243
} // namespace share
} // namespace oceanbase

#define OB_SEEK1243_DROP_STAGE(stage, table_id, table_type, expr) \
  ::oceanbase::share::seek1243::run_drop_stage( \
      (stage), (table_id), static_cast<int64_t>(table_type), [&]() { return (expr); })

#define OB_SEEK1243_DROP_CONNECTION_STAGE(client, stage, table_id, table_type, expr) \
  ::oceanbase::share::seek1243::run_drop_connection_stage( \
      (client), (stage), (table_id), static_cast<int64_t>(table_type), [&]() { return (expr); })

// The conditional form evaluates expr exactly once and keeps non-DROP callers
// on their original path when the shared helper is instrumented on Windows.
#define OB_SEEK1243_DROP_STAGE_IF(enabled, stage, table_id, table_type, expr) \
  ((enabled) ? OB_SEEK1243_DROP_STAGE((stage), (table_id), (table_type), (expr)) : (expr))

#define OB_SEEK1243_DROP_CONNECTION_STAGE_IF(enabled, client, stage, table_id, table_type, expr) \
  ((enabled) ? ::oceanbase::share::seek1243::run_drop_connection_stage( \
      (client), (stage), (table_id), static_cast<int64_t>(table_type), [&]() { return (expr); }) : (expr))

#define OB_SEEK1243_DROP_SCOPE(stage, table_id, table_type, ret) \
  ::oceanbase::share::seek1243::DropStageScope seek1243_drop_scope( \
      (stage), (table_id), static_cast<int64_t>(table_type), (ret))

#define OB_SEEK1243_DROP_CONNECTION_SCOPE(client, stage, table_id, table_type, ret) \
  ::oceanbase::share::seek1243::DropStageScope seek1243_drop_scope( \
      (client), (stage), (table_id), static_cast<int64_t>(table_type), (ret))

#else

namespace oceanbase
{
namespace share
{
namespace seek1243
{
class DropConnectionScope
{
public:
  DropConnectionScope(common::ObISQLClient &, const bool, const uint64_t, const int64_t) {}
};
} // namespace seek1243
} // namespace share
} // namespace oceanbase

#define OB_SEEK1243_DROP_STAGE(stage, table_id, table_type, expr) (expr)
#define OB_SEEK1243_DROP_CONNECTION_STAGE(client, stage, table_id, table_type, expr) (expr)
#define OB_SEEK1243_DROP_STAGE_IF(enabled, stage, table_id, table_type, expr) (expr)
#define OB_SEEK1243_DROP_CONNECTION_STAGE_IF(enabled, client, stage, table_id, table_type, expr) (expr)
#define OB_SEEK1243_DROP_SCOPE(stage, table_id, table_type, ret)
#define OB_SEEK1243_DROP_CONNECTION_SCOPE(client, stage, table_id, table_type, ret)

#endif // OB_SEEK1243_DROP_DIAGNOSTIC
