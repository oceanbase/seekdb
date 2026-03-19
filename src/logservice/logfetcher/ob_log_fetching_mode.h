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
#ifndef OCEANBASE_LOG_FETCHER_FETCHING_MODE_H_
#define OCEANBASE_LOG_FETCHER_FETCHING_MODE_H_

namespace oceanbase
{
namespace logfetcher
{
enum class ClientFetchingMode
{
  FETCHING_MODE_UNKNOWN = 0,

  FETCHING_MODE_INTEGRATED,
  FETCHING_MODE_DIRECT,

  FETCHING_MODE_MAX
};

const char *print_fetching_mode(const ClientFetchingMode mode);
ClientFetchingMode get_fetching_mode(const char *fetching_mode_str);

bool is_fetching_mode_valid(const ClientFetchingMode mode);
bool is_integrated_fetching_mode(const ClientFetchingMode mode);
bool is_direct_fetching_mode(const ClientFetchingMode mode);

} // logservice
} // oceanbase

#endif
