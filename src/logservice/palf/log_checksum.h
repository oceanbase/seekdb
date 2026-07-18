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

#ifndef OCEANBASE_LOGSERVICE_LOG_CHECKSUM_
#define OCEANBASE_LOGSERVICE_LOG_CHECKSUM_

#include <stdint.h>
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace palf
{
class LogChecksum
{
public:
  LogChecksum();
  virtual ~LogChecksum() {}
public:
  int init(const int64_t id, const int64_t accum_checksum);
  void destroy();
  virtual int acquire_accum_checksum(const int64_t data_checksum,
                                     int64_t &accum_checksum);
  virtual int verify_accum_checksum(const int64_t data_checksum,
                                    const int64_t accum_checksum);
  static int verify_accum_checksum(const int64_t old_accum_checksum,
                                   const int64_t data_checksum,
                                   const int64_t expected_accum_checksum,
                                   int64_t &new_accum_checksum);
  virtual void set_accum_checksum(const int64_t accum_checksum);
  virtual void set_verify_checksum(const int64_t verify_checksum);
  virtual int rollback_accum_checksum(const int64_t curr_accum_checksum);
private:
  bool is_inited_;
  int64_t palf_id_;
  int64_t prev_accum_checksum_;
  int64_t accum_checksum_;
  int64_t verify_checksum_;
private:
  DISALLOW_COPY_AND_ASSIGN(LogChecksum);
};
} // namespace palf
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_LOG_CHECKSUM_
