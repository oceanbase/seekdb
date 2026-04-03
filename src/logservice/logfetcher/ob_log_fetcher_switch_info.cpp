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
#include "ob_log_fetcher_switch_info.h"

namespace oceanbase
{
namespace logfetcher
{
const char *print_switch_reason(const KickOutReason reason)
{
  const char *str = "NONE";
  switch (reason) {
    case FETCH_LOG_FAIL_ON_RPC:
      str = "FetchLogFailOnRpc";
      break;

    case FETCH_LOG_FAIL_ON_SERVER:
      str = "FetchLogFailOnServer";
      break;

    case MISSING_LOG_FETCH_FAIL:
      str = "MissingLogFetchFail";
      break;

    case LAGGED_FOLLOWER:
      str = "LAGGED_FOLLOWER";
      break;

    case LOG_NOT_IN_THIS_SERVER:
      str = "LOG_NOT_IN_THIS_SERVER";
      break;

    case LS_OFFLINED:
      str = "LS_OFFLINED";
      break;

    case PROGRESS_TIMEOUT:
      str = "PROGRESS_TIMEOUT";
      break;

    case NEED_SWITCH_SERVER:
      str = "NeedSwitchServer";
      break;

    case DISCARDED:
      str = "Discarded";
      break;

    case ARCHIVE_ITER_END_BUT_LS_NOT_EXIST_IN_PALF:
      str = "ARCHIVE_ITER_END_BUT_LS_NOT_EXIST_IN_PALF";
      break;

    default:
      str = "NONE";
      break;
  }

  return str;
}

} // namespace logfetcher
} // namespace oceanbase
