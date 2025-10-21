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

#define USING_LOG_PREFIX STORAGE

#include "storage/tablet/ob_tablet_service_clog_replay_executor.h"

using namespace oceanbase::logservice;
using namespace oceanbase::share;

namespace oceanbase
{
namespace storage
{
ObTabletServiceClogReplayExecutor::ObTabletServiceClogReplayExecutor()
  : ObTabletReplayExecutor(), buf_(nullptr), buf_size_(0), pos_(0), scn_()
{
}

int ObTabletServiceClogReplayExecutor::init(
    const char *buf,
    const int64_t buf_size,
    const int64_t pos,
    const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
          || OB_UNLIKELY(buf_size <= 0)
          || OB_UNLIKELY(pos < 0)
          || OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments",  KP(buf), K(buf_size), K(pos), K(scn), K(ret));
  } else {
    buf_ = buf;
    buf_size_ = buf_size;
    pos_ = pos;
    scn_ = scn;
    is_inited_ = true;
  } 

  return ret;
}

int ObTabletServiceClogReplayExecutor::do_replay_(ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(handle.get_obj()->replay_update_storage_schema(scn_, buf_, buf_size_, pos_))) {
    LOG_WARN("update tablet storage schema fail", K(ret), K(handle), K_(scn), KP_(buf), K_(buf_size), K_(pos));
  }

  return ret;
}

}
}
