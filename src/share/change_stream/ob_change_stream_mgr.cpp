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

#define USING_LOG_PREFIX SHARE
#include "lib/oblog/ob_log_module.h"
#include "share/change_stream/ob_change_stream_mgr.h"
#include "share/rc/ob_tenant_base.h"
#include "lib/thread/thread_define.h"
#include "share/ob_thread_define.h"

namespace oceanbase
{
namespace share
{

ObChangeStreamMgr::ObChangeStreamMgr()
  : is_inited_(false),
    fetcher_(),
    dispatcher_(),
    worker_()
{
}

ObChangeStreamMgr::~ObChangeStreamMgr()
{
  destroy();
}

int ObChangeStreamMgr::mtl_init(ObChangeStreamMgr *&mgr)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(mgr)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("ObChangeStreamMgr: mgr is null", K(ret));
  } else if (OB_FAIL(mgr->init())) {
    LOG_WARN("ObChangeStreamMgr init failed", KR(ret), "tenant_id", MTL_ID());
  } else {
    LOG_INFO("ObChangeStreamMgr mtl_init success", "tenant_id", MTL_ID(), KP(MTL_CTX()));
  }
  return ret;
}

int ObChangeStreamMgr::init()
{
  int ret = common::OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(fetcher_.init(&dispatcher_))) {
    LOG_WARN("ObChangeStreamMgr: fetcher init failed", K(ret));
  } else if (OB_FAIL(dispatcher_.init())) {
    LOG_WARN("ObChangeStreamMgr: dispatcher init failed", K(ret));
  } else if (OB_FAIL(worker_.init(GET_THREAD_NUM_BY_NPROCESSORS(2)))) {
    LOG_WARN("ObChangeStreamMgr: worker init failed", K(ret));
  } else {
    is_inited_ = true;
    FLOG_INFO("ObChangeStreamMgr init success (Fetcher/Dispatcher/Worker)", K(sysconf(_SC_NPROCESSORS_ONLN)));
  }
  return ret;
}

int ObChangeStreamMgr::start()
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_) {
    ret = common::OB_NOT_INIT;
    LOG_WARN("ObChangeStreamMgr is not inited", K(ret));
  } else {
    if (OB_FAIL(fetcher_.start())) {
      LOG_WARN("ObChangeStreamMgr: fetcher start failed", K(ret));
    } else if (OB_FAIL(dispatcher_.start())) {
      LOG_WARN("ObChangeStreamMgr: dispatcher start failed", K(ret));
    } else if (OB_FAIL(worker_.start())) {
      LOG_WARN("ObChangeStreamMgr: worker start failed", K(ret));
    } else {
      LOG_INFO("ObChangeStreamMgr start success (Fetcher/Dispatcher/Worker threads started)");
    }
  }
  return ret;
}

void ObChangeStreamMgr::stop()
{
  if (is_inited_) {
    fetcher_.stop();
    dispatcher_.stop();
    worker_.stop();
  }
}

void ObChangeStreamMgr::wait()
{
  if (is_inited_) {
    fetcher_.wait();
    dispatcher_.wait();
    worker_.wait();
  }
}

void ObChangeStreamMgr::destroy()
{
  if (is_inited_) {
    stop();
    wait();

    fetcher_.destroy();
    dispatcher_.destroy();
    worker_.destroy();
  }
  is_inited_ = false;
}

}  // namespace share
}  // namespace oceanbase
