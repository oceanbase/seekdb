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

#define USING_LOG_PREFIX RPC_OBRPC
#include "rpc/obrpc/ob_net_client.h"

#include "rpc/obrpc/ob_rpc_proxy.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace obrpc
{
using rpc::frame::ObNetOptions;

ObNetClient::ObNetClient()
    : inited_(false), transport_(NULL)
{
  // empty
}

ObNetClient::~ObNetClient()
{
  destroy();
}

int ObNetClient::init_(const ObNetOptions opts)
{
  int ret = OB_SUCCESS;

  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("Net client init twice", K(ret));
  } else {
    inited_ = true;
  }

  LOG_INFO("net client init", K(ret), "rpc io", opts.rpc_io_cnt_, "mysql io", opts.mysql_io_cnt_);

  return ret;
}

int ObNetClient::init()
{
  int ret = OB_SUCCESS;

  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("Net client init twice", K(ret));
  } else {
    ObNetOptions opts;
    opts.rpc_io_cnt_ = 1;
    opts.mysql_io_cnt_ = 1;

    if (OB_FAIL(init_(opts))) {
      LOG_ERROR("Init client network fail", K(ret));
    }
  }

  return ret;
}


int ObNetClient::init(const ObNetOptions opts)
{
  return init_(opts);
}

void ObNetClient::destroy()
{
  if (inited_) {
    inited_ = false;
    LOG_INFO("net client destory successfully");
  }
}

int ObNetClient::get_proxy(ObRpcProxy &proxy)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("Net client not inited", K(ret));
  } else if (OB_FAIL(proxy.init(transport_))) {
    LOG_ERROR("Init proxy error", K(ret));
  } else {
    //do nothing
  }
  return ret;
}

} // end of namespace obrpc
} // end of namespace oceanbase
