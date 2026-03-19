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
#ifndef OCEANBASE_LOG_FETCHER_FETCH_STREAM_TYPE_H__
#define OCEANBASE_LOG_FETCHER_FETCH_STREAM_TYPE_H__

namespace oceanbase
{
namespace logfetcher
{

// Fetch log stream type
//
// 1. Hot streams: streams that are written to more frequently and have a larger log volume
// 2. Cold streams: streams that have not been written to for a long time and rely on heartbeats to maintain progress
// 3. DDL streams: streams dedicated to serving DDL partitions
//
// Different streams with different Strategies
// 1. Hot streams fetch logs frequently and need to allocate more resources to fetch logs
// 2. Cold streams have no logs for a long time, so they can reduce the frequency of log fetching and heartbeats and use less resources
// 3. DDL streams are always of the hot stream type, ensuring sufficient resources, always real-time, and immune to pauses
enum FetchStreamType
{
  FETCH_STREAM_TYPE_UNKNOWN = -1,
  FETCH_STREAM_TYPE_HOT = 0,        // Hot stream
  FETCH_STREAM_TYPE_COLD = 1,       // Cold stream
  FETCH_STREAM_TYPE_SYS_LS = 2,     // SYS LS stream
  FETCH_STREAM_TYPE_MAX
};

bool is_fetch_stream_type_valid(const FetchStreamType type);
const char *print_fetch_stream_type(FetchStreamType type);

}
}

#endif
