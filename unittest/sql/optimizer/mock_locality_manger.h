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

#ifndef _OB_MOCK_LOCALITY_MANAGER_H_
#define _OB_MOCK_LOCALITY_MANAGER_H_
#undef private
#undef protected
#include <gmock/gmock.h>
#define private public
#define protected public
#include "storage/ob_locality_manager.h"

using namespace oceanbase;
namespace test
{
class MockLocalityManager : public oceanbase::storage::ObLocalityManager
{
public:
  virtual int get_server_locality_array(
      common::ObIArray<share::ObServerLocality> &server_locality_array,
      bool &has_readonly_zone) const
  {
    int ret = OB_SUCCESS;
    UNUSED(server_locality_array);
    UNUSED(has_readonly_zone);
    return ret;
  }
};

}


#endif
