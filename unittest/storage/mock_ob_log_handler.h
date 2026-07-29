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

#ifndef MOCK_OB_LOG_HANDLER_H_
#define MOCK_OB_LOG_HANDLER_H_

#include "logservice/ob_log_handler.h"

namespace oceanbase
{
namespace storage
{

class MockObLogHandler : public logservice::ObILogHandler
{
public:
  MockObLogHandler(){};
  int bootstrap() override { return OB_SUCCESS; }
  virtual bool is_valid() const { return true; }
  virtual int append(const void *buffer,
                     const int64_t nbytes,
                     const share::SCN &ref_scn,
                     const bool need_nonblock,
                     logservice::AppendCb *cb,
                     palf::LSN &lsn,
                     share::SCN &scn)
  {
    UNUSED(need_nonblock);
    UNUSED(buffer);
    UNUSED(nbytes);
    UNUSED(ref_scn);
    UNUSED(cb);
    UNUSED(lsn);
    UNUSED(scn);
    return OB_SUCCESS;
  }

  virtual int append_big_log(const void *buffer,
                             const int64_t nbytes,
                             const share::SCN &ref_scn,
                             const bool need_nonblock,
                             logservice::AppendCb *cb,
                             palf::LSN &lsn,
                             share::SCN &scn)
  {
    UNUSED(need_nonblock);
    UNUSED(buffer);
    UNUSED(nbytes);
    UNUSED(ref_scn);
    UNUSED(cb);
    UNUSED(lsn);
    UNUSED(scn);
    return OB_SUCCESS;
  }

  int append_big_log(const void *buffer,
                     const int64_t nbytes,
                     const share::SCN &ref_scn,
                     const bool need_nonblock,
                     const bool allow_compress,
                     logservice::AppendCb *cb,
                     palf::LSN &lsn,
                     share::SCN &scn) override
  {
    UNUSEDx(buffer, nbytes, ref_scn, need_nonblock, allow_compress, cb, lsn, scn);
    return OB_SUCCESS;
  }

  int get_role(common::ObRole &role, int64_t &proposal_id) const override
  {
    role = common::LEADER;
    proposal_id = palf::PALF_INITIAL_PROPOSAL_ID;
    return OB_SUCCESS;
  }

  virtual int change_access_mode(const int64_t mode_version,
                                 const AccessMode &access_mode,
                                 const share::SCN &ref_scn)
  {
    UNUSED(mode_version);
    UNUSED(access_mode);
    UNUSED(ref_scn);
    return OB_SUCCESS;
  }
  virtual int change_access_mode(const int64_t mode_version,
                                 const AccessMode &access_mode,
                                 const int64_t ref_ts_ns)
  {
    UNUSED(mode_version);
    UNUSED(access_mode);
    UNUSED(ref_ts_ns);
    return OB_SUCCESS;
  }
  virtual int get_pending_end_lsn(palf::LSN &pending_end_lsn) const
  {
    UNUSED(pending_end_lsn);
    return OB_SUCCESS;
  }
  int seek(const palf::LSN &start_lsn,
           palf::PalfBufferIterator &iter)
  {
    UNUSED(start_lsn);
    UNUSED(iter);
    return OB_SUCCESS;
  };
  int seek(const palf::LSN &start_lsn,
           palf::PalfGroupBufferIterator &iter)
  {
    UNUSED(start_lsn);
    UNUSED(iter);
    return OB_SUCCESS;
  };
  int get_end_scn(share::SCN &scn) const
  {
    UNUSEDx(mode_version, access_mode, ref_scn);
    return OB_SUCCESS;
  }

  int get_access_mode(int64_t &mode_version, palf::AccessMode &access_mode) const override
  {
    mode_version = palf::PALF_INITIAL_PROPOSAL_ID;
    access_mode = palf::AccessMode::APPEND;
    return OB_SUCCESS;
  }

  int get_append_mode_initial_scn(share::SCN &initial_scn) const override
  {
    initial_scn = share::SCN::min_scn();
    return OB_SUCCESS;
  }

  int seek(const palf::LSN &lsn, palf::PalfBufferIterator &iter) override
  {
    UNUSEDx(lsn, iter);
    return OB_SUCCESS;
  }

  int seek(const palf::LSN &lsn, palf::PalfGroupBufferIterator &iter) override
  {
    UNUSEDx(lsn, iter);
    return OB_SUCCESS;
  }

  int locate_by_scn_coarsely(const share::SCN &scn, palf::LSN &result_lsn) override
  {
    result_lsn = palf::LSN(scn.get_val_for_inner_table_field());
    return OB_SUCCESS;
  }

  int locate_by_lsn_coarsely(const palf::LSN &lsn, share::SCN &result_scn) override
  {
    UNUSED(lsn);
    result_scn = result_scn_;
    return OB_SUCCESS;
  }

  int get_max_decided_scn_as_leader(share::SCN &scn) const override
  {
    scn.set_max();
    return OB_SUCCESS;
  }

  int advance_base_lsn(const palf::LSN &lsn) override
  {
    base_lsn_ = lsn;
    return OB_SUCCESS;
  }

  int get_begin_lsn(palf::LSN &lsn) const override
  {
    lsn = base_lsn_;
    return OB_SUCCESS;
  }

  int get_end_lsn(palf::LSN &lsn) const override
  {
    lsn = base_lsn_;
    return OB_SUCCESS;
  }

  int get_max_lsn(palf::LSN &lsn) const override
  {
    lsn = base_lsn_;
    return OB_SUCCESS;
  }

  int get_max_scn(share::SCN &scn) const override
  {
    scn.set_max();
    return OB_SUCCESS;
  }

  int get_end_scn(share::SCN &scn) const override
  {
    scn.set_max();
    return OB_SUCCESS;
  }

  int get_palf_base_info(const palf::LSN &base_lsn,
                         palf::PalfBaseInfo &palf_base_info) override
  {
    UNUSED(base_lsn);
    palf_base_info.generate_by_default();
    return OB_SUCCESS;
  }
  int advance_base_info(const palf::PalfBaseInfo &palf_base_info)
  {
    UNUSED(palf_base_info);
    return OB_SUCCESS;
  }
  bool is_replay_enabled() const
  {
    return true;
  }
  int get_leader_config_version(palf::LogConfigVersion &config_version) const
  {
    UNUSED(config_version);
    return OB_SUCCESS;
  }
  int get_member_gc_stat(const common::ObAddr &addr, bool &is_valid_member, obcall::LogMemberGCStat &stat) const
  {
    scn.set_max();
    return OB_SUCCESS;
  }

  int pend_submit_replay_log() override { return OB_SUCCESS; }
  int restore_submit_replay_log() override { return OB_SUCCESS; }
  bool is_replay_enabled() const override { return true; }
  int offline() override { return OB_SUCCESS; }

  int online(const palf::LSN &lsn, const share::SCN &scn) override
  {
    UNUSEDx(lsn, scn);
    return OB_SUCCESS;
  }

  LSN base_lsn_;
  int64_t result_ts_ns_;
  share::SCN result_scn_;
  int enable_replay(const palf::LSN &initial_lsn,
                    const share::SCN &initial_scn)
  {
    UNUSED(initial_lsn);
    UNUSED(initial_scn);
    return OB_SUCCESS;
  }
  int enable_replay(const palf::LSN &initial_lsn,
                    const int64_t &initial_log_ts)
  {
    UNUSED(initial_lsn);
    UNUSED(initial_log_ts);
    return OB_SUCCESS;
  }
  int disable_replay()
  {
    return OB_SUCCESS;
  }
  int get_max_decided_scn(share::SCN &scn)
  {
    scn.set_max();
    return OB_SUCCESS;
  }
  int get_max_decided_scn_as_leader(share::SCN &scn) const
  {
    scn.set_max();
    return OB_SUCCESS;
  }
  int get_max_decided_log_ts_ns(int64_t &log_ts)
  {
    log_ts = INT64_MAX;
    return OB_SUCCESS;
  }
  int get_election_leader(common::ObAddr &addr) const
  {
    UNUSED(addr);
    return OB_SUCCESS;
  }
  int get_parent(common::ObAddr &parent) const
  {
    UNUSED(parent);
    return OB_SUCCESS;
  }
  bool is_offline() const {return false;};
  int offline() {return OB_SUCCESS;};
  int online(const LSN &lsn, const share::SCN &scn) { UNUSED(lsn); UNUSED(scn); return OB_SUCCESS;};
  int is_replay_fatal_error(bool &has_fatal_error) {has_fatal_error = false; return OB_SUCCESS;}
};

} // namespace storage
} // namespace oceanbase

#endif
