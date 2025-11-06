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

#include "ob_replay_handler.h"
#include "logservice/ob_log_service.h"

namespace oceanbase
{
using namespace palf;
using namespace share;
namespace logservice
{
ObReplayHandler::ObReplayHandler(storage::ObLS *ls)
{
  reset();
  ls_ = ls;
}

ObReplayHandler::~ObReplayHandler()
{
  reset();
}

void ObReplayHandler::reset()
{
  for (int i = 0; i < ObLogBaseType::MAX_LOG_BASE_TYPE; i++) {
    handlers_[i] = NULL;
  }
  ls_ = NULL;
}

int ObReplayHandler::register_handler(const ObLogBaseType &type,
                                      ObIReplaySubHandler *handler)
{
  int ret = OB_SUCCESS;

  if (!is_valid_log_base_type(type) || NULL == handler) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid arguments", K(ret), K(type), K(handler));
  } else {
    WLockGuard guard(lock_);
    handlers_[type] = handler;
  }

  return ret;
}

void ObReplayHandler::unregister_handler(const ObLogBaseType &type)
{
  int ret = OB_SUCCESS;

  if (!is_valid_log_base_type(type)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid arguments", K(ret), K(type));
  } else {
    WLockGuard guard(lock_);
    handlers_[type] = NULL;
  }
}

int ObReplayHandler::replay(const ObLogBaseType &type,
                            const void *buffer,
                            const int64_t nbytes,
                            const palf::LSN &lsn,
                            const SCN &scn)
{
  int ret = OB_SUCCESS;

  RLockGuard guard(lock_);
  if (!is_valid_log_base_type(type)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid arguments", K(ret), K(type));
  } else if (OB_ISNULL(handlers_[type])) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "invalid base_log_type", K(type));
  } else {
    ret = handlers_[type]->replay(buffer, nbytes, lsn, scn);
  }

  return ret;
}

} // namespace logservice
} // namespace oceanbase
