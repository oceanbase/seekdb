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
#include <cstring>
#include "lib/utility/ob_macro_utils.h"
#include "ob_log_fetching_mode.h"

namespace oceanbase
{
namespace logfetcher
{
const char *print_fetching_mode(const ClientFetchingMode mode)
{
  const char *mode_str = "INVALID";
  switch (mode) {
    case ClientFetchingMode::FETCHING_MODE_INTEGRATED: {
      mode_str = "Integrated Fetching Mode";
      break;
    }

    case ClientFetchingMode::FETCHING_MODE_DIRECT: {
      mode_str = "Direct Fetching Mode";
      break;
    }

    default: {
      mode_str = "INVALID";
      break;
    }

  }
  return mode_str;
}

ClientFetchingMode get_fetching_mode(const char *fetching_mode_str)
{
  ClientFetchingMode fetching_mode = ClientFetchingMode::FETCHING_MODE_UNKNOWN;
  if (OB_ISNULL(fetching_mode_str)) {

  } else if (0 == strcmp(fetching_mode_str, "integrated")) {
    fetching_mode = ClientFetchingMode::FETCHING_MODE_INTEGRATED;
  } else if (0 == strcmp(fetching_mode_str, "direct")) {
    fetching_mode = ClientFetchingMode::FETCHING_MODE_DIRECT;
  } else {

  }
  return fetching_mode;
}

bool is_fetching_mode_valid(const ClientFetchingMode mode)
{
  return mode > ClientFetchingMode::FETCHING_MODE_UNKNOWN &&
         mode < ClientFetchingMode::FETCHING_MODE_MAX;
}

bool is_integrated_fetching_mode(const ClientFetchingMode mode)
{
  return ClientFetchingMode::FETCHING_MODE_INTEGRATED == mode;
}

bool is_direct_fetching_mode(const ClientFetchingMode mode)
{
  return ClientFetchingMode::FETCHING_MODE_DIRECT == mode;
}

}
}
