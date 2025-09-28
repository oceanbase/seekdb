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

#include "ob_trans_event.h"
#include "lib/stat/ob_session_stat.h"

namespace oceanbase
{
using namespace common;

namespace transaction
{


void ObTransStatItem::reset()
{
  for (int64_t i = 0; i < STAT_BUFFER_COUNT; i++) {
    stat_buf_[i] = 0;
  }
  total_ = 0;
  save_ = 0;
  stat_idx_ = 0;
  last_time_ = 0;
}


int64_t ObTransStatItem::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  databuff_printf(buf, buf_len , pos, "[");
  for (int64_t i = 0; i < STAT_BUFFER_COUNT; ++i) {
    (void)databuff_print_obj(buf, buf_len, pos, stat_buf_[i]);
    (void)databuff_printf(buf, buf_len , pos, ", ");
  }
  databuff_printf(buf, buf_len , pos, "]");

  return pos;
}



void ObTransStatistic::add_commit_trans_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_COMMIT_COUNT, value);
}

void ObTransStatistic::add_rollback_trans_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_ROLLBACK_COUNT, value);
}

void ObTransStatistic::add_trans_start_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_START_COUNT, value);
}
void ObTransStatistic::add_trans_timeout_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_TIMEOUT_COUNT, value);
}

void ObTransStatistic::add_trans_total_used_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_TOTAL_USED_TIME, value);
  //trans_total_used_time_stat_.add(value);
}

void ObTransStatistic::add_read_elr_row_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(READ_ELR_ROW_COUNT, value);
}





void ObTransStatistic::add_clog_submit_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_COMMIT_LOG_SUBMIT_COUNT, value);
  //clog_submit_count_stat_.add(value);
}

void ObTransStatistic::add_clog_sync_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_COMMIT_LOG_SYNC_TIME, value);
  //clog_sync_time_stat_.add(value);
}

void ObTransStatistic::add_clog_sync_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_COMMIT_LOG_SYNC_COUNT, value);
  //clog_sync_count_stat_.add(value);
}

void ObTransStatistic::add_trans_commit_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_COMMIT_TIME, value);
  //trans_commit_time_stat_.add(value);
}

void ObTransStatistic::add_trans_rollback_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_ROLLBACK_TIME, value);
  //trans_rollback_time_stat_.add(value);
}

void ObTransStatistic::add_readonly_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_READONLY_COUNT, value);
}

void ObTransStatistic::add_local_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_LOCAL_COUNT, value);
}

void ObTransStatistic::add_dist_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_DIST_COUNT, value);
}

void ObTransStatistic::add_redo_log_replay_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(REDO_LOG_REPLAY_COUNT, value);
}

void ObTransStatistic::add_redo_log_replay_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(REDO_LOG_REPLAY_TIME, value);
}

void ObTransStatistic::add_prepare_log_replay_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(PREPARE_LOG_REPLAY_COUNT, value);
}

void ObTransStatistic::add_prepare_log_replay_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(PREPARE_LOG_REPLAY_TIME, value);
}

void ObTransStatistic::add_commit_log_replay_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(COMMIT_LOG_REPLAY_COUNT, value);
}

void ObTransStatistic::add_commit_log_replay_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(COMMIT_LOG_REPLAY_TIME, value);
}

void ObTransStatistic::add_abort_log_replay_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(ABORT_LOG_REPLAY_COUNT, value);
}

void ObTransStatistic::add_abort_log_replay_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(ABORT_LOG_REPLAY_TIME, value);
}

void ObTransStatistic::add_clear_log_replay_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(CLEAR_LOG_REPLAY_COUNT, value);
}

void ObTransStatistic::add_clear_log_replay_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(CLEAR_LOG_REPLAY_TIME, value);
}



















void ObTransStatistic::add_gts_request_total_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(GTS_REQUEST_TOTAL_COUNT, value);
}
void ObTransStatistic::add_gts_acquire_total_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(GTS_ACQUIRE_TOTAL_TIME, value);
}
void ObTransStatistic::add_gts_acquire_total_wait_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  //EVENT_ADD(GTS_ACQUIRE_TOTAL_WAIT_COUNT, value);
}
void ObTransStatistic::add_gts_wait_elapse_total_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(GTS_WAIT_ELAPSE_TOTAL_TIME, value);
}
void ObTransStatistic::add_gts_wait_elapse_total_wait_count(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(GTS_WAIT_ELAPSE_TOTAL_WAIT_COUNT, value);
}







void ObTransStatistic::add_trans_log_total_size(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(CLOG_TRANS_LOG_TOTAL_SIZE, value);
}

void ObTransStatistic::add_local_trans_total_used_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_LOCAL_TOTAL_USED_TIME, value);
}
void ObTransStatistic::add_dist_trans_total_used_time(const uint64_t tenant_id, const int64_t value)
{
  ObTenantDiagnosticInfoSummaryGuard g(tenant_id, 0);
  EVENT_ADD(TRANS_DIST_TOTAL_USED_TIME, value);
}

} // transaction
} // oceanbase
