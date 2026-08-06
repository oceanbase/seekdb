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

#ifndef _OCEABASE_OBSERVER_OB_SRV_NETWORK_FRAME_H_
#define _OCEABASE_OBSERVER_OB_SRV_NETWORK_FRAME_H_

#include "rpc/frame/ob_req_translator.h"
#include "observer/ob_srv_xlator.h"
#include "observer/ob_srv_deliver.h"
#include "observer/ob_server_struct.h"

namespace oceanbase {
namespace rpc {
namespace frame {
class ObReqTranslator;
}}}

namespace oceanbase
{
namespace observer
{

class ObSrvNetworkFrame
{
public:
  explicit ObSrvNetworkFrame(ObGlobalContext &gctx);

  virtual ~ObSrvNetworkFrame();

  int init();
  void destroy();
  int start();
  void sql_nio_stop();
  void wait();
  int stop();

  int reload_config();
  ObSrvDeliver& get_deliver() { return deliver_; }
  inline rpc::frame::ObReqTranslator &get_xlator();

private:
  ObGlobalContext &gctx_;

  ObSrvXlator xlator_;
  rpc::frame::ObReqQHandler request_qhandler_;

  // generic deliver
  ObSrvDeliver deliver_;

  DISALLOW_COPY_AND_ASSIGN(ObSrvNetworkFrame);
}; // end of class ObSrvNetworkFrame

// inline functions
inline
rpc::frame::ObReqTranslator &
ObSrvNetworkFrame::get_xlator() {
  return xlator_;
}

static int get_default_net_thread_count()
{
  int cnt = 1;
  int cpu_num = static_cast<int>(get_cpu_count());

  if (cpu_num <= 2) {
    cnt = 1;
  } else if (cpu_num <= 4) {
    cnt = 2;
  } else if (cpu_num <= 8) {
    cnt = 3;
  } else if (cpu_num <= 16) {
    cnt = cpu_num / 2;
  } else if (cpu_num <= 32) {
    cnt = 6 + cpu_num / 8;
  } else {
    cnt = max(10, 4 + cpu_num / 6);
    cnt = min(cnt, 128);
  }
  return cnt;
}

} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OCEABASE_OBSERVER_OB_SRV_NETWORK_FRAME_H_ */
