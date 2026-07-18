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

#ifndef OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_RECONFIRM_
#define OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_RECONFIRM_
#include "logservice/palf/log_reconfirm.h"

namespace oceanbase
{
namespace palf
{

class MockLogReconfirm : public LogReconfirm
{
public:
  MockLogReconfirm() : mock_ret_(OB_SUCCESS) {}
  ~MockLogReconfirm() {}

  int init(const int64_t palf_id,
           const ObAddr &self,
           LogSlidingWindow *sw,
           LogStateMgr *state_mgr,
           LogConfigMgr *mm,
           LogEngine *log_engine)
  {
    int ret = OB_SUCCESS;
    UNUSED(palf_id);
    UNUSED(self);
    UNUSED(sw);
    UNUSED(state_mgr);
    UNUSED(mm);
    UNUSED(log_engine);
    return ret;
  }
  void destroy() {}
  void reset_state() {}
  bool need_start_up()
  {
    return false;
  }
  int reconfirm()
  {
    return mock_ret_;
  }
  int handle_prepare_response(const common::ObAddr &server,
                              const int64_t &src_proposal_id,
                              const int64_t &accept_proposal_id,
                              const LSN &last_lsn)
  {
    int ret = OB_SUCCESS;
    UNUSED(server);
    UNUSED(src_proposal_id);
    UNUSED(accept_proposal_id);
    UNUSED(last_lsn);
    return ret;
  }
  int receive_log(const common::ObAddr &server,
                  const LSN &prev_lsn,
                  const int64_t &prev_proposal_id,
                  const LSN &lsn,
                  const char *buf,
                  const int64_t buf_len)
  {
    int ret = OB_SUCCESS;
    UNUSED(server);
    UNUSED(prev_lsn);
    UNUSED(prev_proposal_id);
    UNUSED(lsn);
    UNUSED(buf);
    UNUSED(buf_len);
    return ret;
  }
public:
  int mock_ret_;
};

} // end of palf
} // end of oceanbase

#endif
