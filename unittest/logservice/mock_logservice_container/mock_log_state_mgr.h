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

#ifndef OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_STATE_MGR_
#define OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_STATE_MGR_

#define private public
#include "logservice/palf/log_state_mgr.h"
#undef private

namespace oceanbase
{
namespace palf
{

class MockLogStateMgr : public LogStateMgr
{
public:
  MockLogStateMgr()
    : can_append_(true), can_slide_sw_(true), state_(ACTIVE)
  {}
  ~MockLogStateMgr() override = default;

  void destroy() override {}

  int init(const common::ObAddr &self,
           LogSlidingWindow *sw,
           LogModeMgr *mode_mgr) override
  {
    UNUSEDx(self, sw, mode_mgr);
    return OB_SUCCESS;
  }

  bool is_state_changed() override { return false; }
  int switch_state() override { return OB_SUCCESS; }
  int set_scan_disk_log_finished() override { return OB_SUCCESS; }
  bool can_append() const override { return can_append_; }
  bool can_slide_sw() const override { return can_slide_sw_; }
  LogState get_state() const override { return state_; }
  bool need_freeze_group_buffer() const override { return ACTIVE == state_; }
  bool is_active() const override { return ACTIVE == state_; }
  bool is_recovering() const override { return RECOVERING == state_; }

public:
  bool can_append_;
  bool can_slide_sw_;
  LogState state_;
};

} // namespace palf
} // namespace oceanbase

#endif
