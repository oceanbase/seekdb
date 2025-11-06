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

#ifndef OCEANBASE_STORAGE_OB_LOB_RETRY_H_
#define OCEANBASE_STORAGE_OB_LOB_RETRY_H_

#include "storage/lob/ob_lob_access_param.h"

namespace oceanbase
{
namespace storage
{

class ObLobRetryUtil
{
public:
  static bool is_remote_ret_can_retry(int ret_code);
  static int check_need_retry(ObLobAccessParam &param, const int error_code, const int retry_cnt, bool &need_retry);

};

}  // storage
}  // oceanbase

#endif  // OCEANBASE_STORAGE_OB_LOB_RETRY_H_
