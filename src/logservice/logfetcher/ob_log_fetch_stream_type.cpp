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
#include "ob_log_fetch_stream_type.h"

namespace oceanbase
{
namespace logfetcher
{
bool is_fetch_stream_type_valid(const FetchStreamType type)
{
  return type > FETCH_STREAM_TYPE_UNKNOWN && type < FETCH_STREAM_TYPE_MAX;
}

const char *print_fetch_stream_type(FetchStreamType type)
{
  const char *str = nullptr;

  switch (type) {
    case FETCH_STREAM_TYPE_UNKNOWN:
      str = "UNKNOWN";
      break;
    case FETCH_STREAM_TYPE_HOT:
      str = "HOT";
      break;
    case FETCH_STREAM_TYPE_COLD:
      str = "COLD";
      break;
    case FETCH_STREAM_TYPE_SYS_LS:
      str = "SYS_LS";
      break;
    default:
      str = "INVALID";
      break;
  }

  return str;
}

}
}
