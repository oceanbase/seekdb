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

#ifndef OCEANBASE_TRANSACTION_OB_TRANS_ID_SERVICE_
#define OCEANBASE_TRANSACTION_OB_TRANS_ID_SERVICE_

#include "ob_id_service.h"
#include "ob_gti_rpc.h"

namespace oceanbase
{

namespace transaction
{

class ObTransIDService :  public ObIDService
{
public:
  ObTransIDService() {}
  ~ObTransIDService() {}
  int init();
  static int mtl_init(ObTransIDService *&trans_id_service);
  void destroy() { reset(); }
  static const int64_t TRANS_ID_PREALLOCATED_RANGE = 1000000; // TransID default preallocated size
  int handle_request(const ObGtiRequest &request, obrpc::ObGtiRpcResult &result);
};

}
}
#endif
