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

#ifndef OCEANBASE_TRANSACTION_OB_UNIQUE_ID_SERVICE_
#define OCEANBASE_TRANSACTION_OB_UNIQUE_ID_SERVICE_

#include "ob_trans_service.h"

namespace oceanbase
{

namespace transaction
{

class ObUniqueIDService
{
public:
  ObUniqueIDService() {}
  ~ObUniqueIDService() {}
  static int mtl_init(ObUniqueIDService *&unique_id_service)
  {
    return OB_SUCCESS;
  }
  void destroy() {}
  int gen_unique_id(int64_t &unique_id, const int64_t timeout_ts)
  {
    int ret = OB_SUCCESS;
    ObTransID trans_id;
    int64_t expire_ts = ObTimeUtility::current_time() + timeout_ts;
    
    do {
      if (OB_SUCC(MTL(transaction::ObTransService *)->gen_trans_id(trans_id))) {
        unique_id = trans_id.get_id();
      } else if (OB_GTI_NOT_READY == ret) {
        if (ObTimeUtility::current_time() > expire_ts) {
          ret = OB_NEED_RETRY;
          TRANS_LOG(WARN, "get unique id not ready", K(ret), K(expire_ts));
        } else {
          ob_usleep(1000);
        }
      } else {
        TRANS_LOG(WARN, "get unique id fail", KR(ret));
      }
    } while (OB_GTI_NOT_READY == ret);
    return ret;
  }
};

}
}
#endif
