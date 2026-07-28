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

#ifndef OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_MODE_MGR_
#define OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_MODE_MGR_

#define private public
#include "logservice/palf/log_mode_mgr.h"
#undef private

namespace oceanbase
{
namespace palf
{

class MockLogModeMgr : public LogModeMgr
{
public:
  MockLogModeMgr()
  {
    LogModeMeta mode_meta;
    (void) mode_meta.generate(AccessMode::APPEND,
                              share::SCN::min_scn());
    applied_mode_meta_ = mode_meta;
    is_inited_ = true;
  }
  ~MockLogModeMgr() override = default;

  void destroy() override {}
  int get_access_mode(AccessMode &access_mode) const override
  {
    access_mode = applied_mode_meta_.access_mode_;
    return OB_SUCCESS;
  }
  int get_access_mode_ref_scn(AccessMode &access_mode,
                              share::SCN &ref_scn) const override
  {
    access_mode = applied_mode_meta_.access_mode_;
    ref_scn = applied_mode_meta_.ref_scn_;
    return OB_SUCCESS;
  }
};

} // namespace palf
} // namespace oceanbase

#endif
