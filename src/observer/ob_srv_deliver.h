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

#ifndef _OCEABASE_OBSERVER_OB_SRV_DELIVER_H_
#define _OCEABASE_OBSERVER_OB_SRV_DELIVER_H_

#include "rpc/frame/ob_req_deliver.h"

namespace oceanbase
{

namespace observer
{

using rpc::frame::ObiReqQHandler;

class ObSrvDeliver
    : public rpc::frame::ObReqQDeliver
{
public:
  explicit ObSrvDeliver(ObiReqQHandler &qhandler);

  int repost(void* node);
  virtual int deliver(rpc::ObRequest &req);
private:
  int deliver_mysql_request(rpc::ObRequest &req);

private:
  DISALLOW_COPY_AND_ASSIGN(ObSrvDeliver);
}; // end of class ObSrvDeliver

} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OCEABASE_OBSERVER_OB_SRV_DELIVER_H_ */
