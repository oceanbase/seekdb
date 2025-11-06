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

#define USING_LOG_PREFIX RPC_FRAME


#include "ob_req_transport.h"
#include "util/easy_mod_stat.h"
#include "rpc/frame/ob_net_easy.h"
#include "lib/stat/ob_diagnostic_info_guard.h"

using namespace oceanbase::common;
using namespace oceanbase::rpc;
using namespace oceanbase::obrpc;
using namespace oceanbase::rpc::frame;

int ObReqTransport::AsyncCB::on_error(int)
{
  /*
   * To avoid confusion, we implement this new interface to notify exact error
   * type to upper-layer modules.
   *
   * For backward compatibility, this function returns OB_ERROR. The derived
   * classes should overwrite this function and return OB_SUCCESS.
   */
  return OB_ERROR;
}

ObReqTransport::ObReqTransport(
    easy_io_t *eio, easy_io_handler_pt *handler)
    : eio_(eio), handler_(handler), sgid_(0), bucket_count_(0), ratelimit_enabled_(0)
{
  // empty
}
