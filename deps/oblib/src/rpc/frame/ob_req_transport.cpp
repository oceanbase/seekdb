/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
