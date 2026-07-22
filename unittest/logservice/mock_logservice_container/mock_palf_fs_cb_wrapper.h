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

#ifndef OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_PALF_FS_CB_WRAPPER_
#define OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_PALF_FS_CB_WRAPPER_

#include "logservice/palf/palf_callback_wrapper.h"

namespace oceanbase
{
namespace palf
{

class MockPalfFSCbWrapper : public PalfFSCbWrapper {
public:
  MockPalfFSCbWrapper() {}
  ~MockPalfFSCbWrapper() {}

  int add_cb_impl(PalfFSCbNode *cb_impl) override
  {
    int ret = OB_SUCCESS;
    UNUSED(cb_impl);
    return ret;
  }
  void del_cb_impl(PalfFSCbNode *cb_impl) override
  {
    UNUSED(cb_impl);
  }
  int update_end_lsn(const LSN &end_lsn,
                     const share::SCN &end_scn) override
  {
    int ret = OB_SUCCESS;
    UNUSED(end_lsn);
    UNUSED(end_scn);
    return ret;
  }
};

} // end of palf
} // end of oceanbase

#endif
