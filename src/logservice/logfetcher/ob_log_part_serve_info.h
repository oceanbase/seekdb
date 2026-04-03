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
#ifndef OCEANBASE_LOG_FETCHER_DATA_FILTER_H__
#define OCEANBASE_LOG_FETCHER_DATA_FILTER_H__

#include "ob_log_utils.h"       // NTS_TO_STR

namespace oceanbase
{
namespace logfetcher
{

struct PartServeInfo
{
  PartServeInfo() : start_serve_from_create_(false), start_serve_timestamp_(0)
  {
  }
  ~PartServeInfo() {}

  void reset()
  {
    start_serve_from_create_ = false;
    start_serve_timestamp_ = 0;
  }

  void reset(const bool start_serve_from_create, const int64_t start_serve_timestamp)
  {
    start_serve_from_create_ = start_serve_from_create;
    start_serve_timestamp_ = start_serve_timestamp;
  }

  // Determine if a partitioned transaction is in service
  // 1. if the input parameter is prepare log timestamp, return no service must not be served; return service must be served
  // 2. If the input parameter is commit log timestamp, return no service must be no service; return service must be no service
  bool is_served(const int64_t tstamp) const
  {
    bool bool_ret = false;

    // If a partition is served from the moment it is created, all its partition transactions are served
    if (start_serve_from_create_) {
      bool_ret = true;
    } else {
      // Otherwise, require the prepare log timestamp to be greater than or equal to the start service timestamp before the partitioned transaction is serviced
      bool_ret  = (tstamp >= start_serve_timestamp_);
    }

    return bool_ret;
  }

  TO_STRING_KV(K_(start_serve_from_create),
      "start_serve_timestamp", NTS_TO_STR(start_serve_timestamp_));

  bool      start_serve_from_create_;   // If start service from partition creation or not
  uint64_t  start_serve_timestamp_;     // Timestamp of the starting service
};

}
}

#endif
