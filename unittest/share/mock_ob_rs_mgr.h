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

#ifndef OCEANBASE_SHARE_OB_MOCK_RS_MGR_H_
#define OCEANBASE_SHARE_OB_MOCK_RS_MGR_H_

#include "share/ob_rs_mgr.h"

namespace oceanbase
{
namespace share
{
class MockObRsMgr : public ObRsMgr
{
public:
  MOCK_CONST_METHOD1(get_master_root_server, int(common::ObAddr &));
  virtual ~MockObRsMgr() {}

  int get_master_root_server_wrapper(common::ObAddr &rs) { rs = global_rs(); return common::OB_SUCCESS; }

  static common::ObAddr &global_rs() { static common::ObAddr addr; return addr; }
};
}//end namespace share
}//end namespace oceanbase

#endif
