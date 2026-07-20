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
#define USING_LOG_PREFIX RS_COMPACTION
#include "rootserver/freeze/ob_major_merge_progress_util.h"
#include "src/share/ob_tablet_replica_checksum_operator.h"

namespace oceanbase
{
using namespace share;
using namespace common;
namespace compaction
{
ObTableCompactionInfo &ObTableCompactionInfo::operator=(const ObTableCompactionInfo &other)
{
  table_id_ = other.table_id_;
  tablet_cnt_ = other.tablet_cnt_;
  status_ = other.status_;
  unfinish_index_cnt_ = other.unfinish_index_cnt_;
  need_check_fts_ = other.need_check_fts_;
  return *this;
}

const char *ObTableCompactionInfo::TableStatusStr[] = {
  "INITIAL",
  "COMPACTED",
  "CAN_SKIP_VERIFYING",
  "INDEX_CKM_VERIFIED",
  "VERIFIED"
};

const char *ObTableCompactionInfo::status_to_str(const Status &status)
{
  STATIC_ASSERT(static_cast<int64_t>(TB_STATUS_MAX) == ARRAYSIZEOF(TableStatusStr), "table status str len is mismatch");
  const char *str = "";
  if (status < INITIAL || status >= TB_STATUS_MAX) {
    str = "invalid_status";
  } else {
    str = TableStatusStr[status];
  }
  return str;
}

ObTableCompactionInfo::ObTableCompactionInfo()
  : table_id_(OB_INVALID_ID),
    tablet_cnt_(0),
    unfinish_index_cnt_(INVALID_INDEX_CNT),
    status_(Status::INITIAL),
    need_check_fts_(false)
{
}
/**
 * -------------------------------------------------------------------ObMergeProgress-------------------------------------------------------------------
 */
int64_t ObMergeProgress::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
  } else {
    J_OBJ_START();
    if (merge_finish_) {
      J_KV(K_(merge_finish), K_(total_table_cnt));
    } else {
      J_KV(KP(this), K_(merge_finish), K_(unmerged_tablet_cnt), K_(merged_tablet_cnt), K_(total_table_cnt));
      for (int64_t i = 0; i < RECORD_TABLE_TYPE_CNT; ++i) {
        J_COMMA();
        J_KV(ObTableCompactionInfo::TableStatusStr[i], table_cnt_[i]);
      }
    }
    J_OBJ_END();
  }
  return pos;
}

/**
 * -------------------------------------------------------------------ObUncompactInfo-------------------------------------------------------------------
 */
ObUncompactInfo::ObUncompactInfo()
  : diagnose_rw_lock_(ObLatchIds::MAJOR_FREEZE_DIAGNOSE_LOCK),
    tablets_(),
    table_ids_()
{}

ObUncompactInfo::~ObUncompactInfo()
{
  reset();
}

void ObUncompactInfo::reset()
{
  SpinWLockGuard w_guard(diagnose_rw_lock_);
  tablets_.reuse();
  table_ids_.reuse();
  skip_verify_tables_.reuse();
}

void ObUncompactInfo::add_table(const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard w_guard(diagnose_rw_lock_);
  if (table_ids_.count() < DEBUG_INFO_CNT
      && OB_FAIL(table_ids_.push_back(table_id))) {
    LOG_WARN("fail to push_back", KR(ret), K(table_id));
  }
}

void ObUncompactInfo::add_skip_verify_table(const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  // no need lock, just print log, not show in virtual_table
  if (skip_verify_tables_.count() < SKIP_VERIFY_TABLE_CNT
      && OB_FAIL(skip_verify_tables_.push_back(table_id))) {
    LOG_WARN("fail to push_back", KR(ret), K(table_id));
  }
}

void ObUncompactInfo::add_tablet(const share::ObTabletReplica &replica)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard w_guard(diagnose_rw_lock_);
  if (tablets_.count() < DEBUG_INFO_CNT
      && OB_FAIL(tablets_.push_back(replica))) {
    LOG_WARN("fail to push_back", KR(ret), K(replica));
  }
}

void ObUncompactInfo::add_tablet(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObTabletReplica fake_replica;
  fake_replica.fake_for_diagnose(tablet_id);
  SpinWLockGuard w_guard(diagnose_rw_lock_);
  if (tablets_.count() < DEBUG_INFO_CNT
      && OB_FAIL(tablets_.push_back(fake_replica))) {
    LOG_WARN("fail to push_back", KR(ret), K(fake_replica));
  }
}

int ObUncompactInfo::get_uncompact_info(
    ObIArray<ObTabletReplica> &input_tablets,
    ObIArray<uint64_t> &input_table_ids) const
{
  int ret = OB_SUCCESS;
  SpinRLockGuard r_guard(diagnose_rw_lock_);
  if (OB_FAIL(input_tablets.assign(tablets_))) {
    LOG_WARN("fail to assign uncompacted_tablets", KR(ret), K_(tablets));
  } else if (OB_FAIL(input_table_ids.assign(table_ids_))) {
    LOG_WARN("fail to assign uncompacted_tablets", KR(ret), K_(table_ids));
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
