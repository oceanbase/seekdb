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

#include "logservice/palf/i_log_role_change_cb.h"
#include "logservice/palf/i_log_replay_engine.h"
#include "logservice/palf/i_log_apply_service.h"
#include "logservice/palf/i_log_role_change_cb.h"
#include "lib/ob_define.h"
#include <gtest/gtest.h>
namespace oceanbase
{
namespace unittest
{
using namespace common;
using namespace palf;
class MockLogCtx : public palf::ILogCtx
{
public:
  explicit MockLogCtx(int64_t cb_id) :cb_id_(cb_id)
  {}
  ~MockLogCtx() {}
  virtual int on_success();
  // The function will be called when the log has not formed a majority, after which the object will no longer be used
  virtual int on_failure();
private:
  int64_t cb_id_;
};
class MockRoleChangeCB : public palf::ILogRoleChangeCB
{
public:
  MockRoleChangeCB() {}
  virtual ~MockRoleChangeCB() {}
public:
  virtual int on_leader_revoke(const int64_t palf_id)
  {
    UNUSED(palf_id);
    return OB_SUCCESS;
  }
  virtual int is_leader_revoke_done(const int64_t palf_id,
                                    bool &is_done) const
  {
    UNUSED(palf_id);
    UNUSED(is_done);
    return OB_SUCCESS;
  }
  virtual int on_leader_takeover(const int64_t palf_id)
  {
    UNUSED(palf_id);
    return OB_SUCCESS;
  }
  virtual int is_leader_takeover_done(const int64_t palf_id,
                                      bool &is_done) const
  {
    UNUSED(palf_id);
    UNUSED(is_done);
    return OB_SUCCESS;
  }
  virtual int on_leader_active(const int64_t palf_id)
  {
    UNUSED(palf_id);
    return OB_SUCCESS;
  }
};
class MockLogReplayEngine : public palf::ILogReplayEngine
{
public:
  MockLogReplayEngine() {}
  virtual ~MockLogReplayEngine() {}
public:
  virtual int is_replay_done(const int64_t palf_id,
                             bool &is_done) const
  {
    UNUSED(palf_id);
    is_done = true;
    return OB_SUCCESS;
  }
  virtual int update_lsn_to_replay(const int64_t palf_id,
                                          const LSN &lsn)
  {
    UNUSED(palf_id);
    UNUSED(lsn);
    return OB_SUCCESS;
  }
};

class MockLogApplyEngine : public palf::ILogApplyEngine
{
public:
  MockLogApplyEngine() {}
  virtual ~MockLogApplyEngine() {}
public:
  virtual int submit_apply_task(logservice::AppendCb *cb)
  {
    UNUSED(cb);
    return OB_SUCCESS;
  }
};
} // end of unittest
} // end of oceanbase
