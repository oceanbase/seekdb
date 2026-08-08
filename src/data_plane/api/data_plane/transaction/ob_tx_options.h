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

#ifndef OCEANBASE_DATA_PLANE_API_OB_TX_OPTIONS_H_
#define OCEANBASE_DATA_PLANE_API_OB_TX_OPTIONS_H_

#include <cstdint>
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace transaction
{

enum class ObTxClass { USER, SYS };

enum class ObTxConsistencyType
{
  INVALID = 0,
  CURRENT_READ = 1,
  BOUNDED_STALENESS_READ = 2,
};

enum class ObTxIsolationLevel
{
  INVALID = -1,
  RU = 0,
  RC = 1,
  RR = 2,
  SERIAL = 3,
};

extern ObTxIsolationLevel tx_isolation_from_str(const common::ObString &str);

inline bool is_RR_or_SERIAL_isolevel(const ObTxIsolationLevel isolation)
{
  return isolation == ObTxIsolationLevel::RR ||
         isolation == ObTxIsolationLevel::SERIAL;
}

inline bool is_RC_isolevel(const ObTxIsolationLevel isolation)
{
  return isolation == ObTxIsolationLevel::RC;
}

enum class ObTxAccessMode
{
  INVL = -1,
  RW = 0,
  RD_ONLY = 1,
};

enum ObTxCleanPolicy
{
  FAST_ROLLBACK = 1,
  ROLLBACK = 2,
  KEEP = 3,
};

struct ObTxParam
{
  ObTxParam();
  bool is_valid() const;
  ~ObTxParam();

  int64_t timeout_us_;
  int64_t lock_timeout_us_;
  ObTxAccessMode access_mode_;
  ObTxIsolationLevel isolation_;

  TO_STRING_KV(K_(timeout_us),
               K_(lock_timeout_us),
               K_(access_mode),
               K_(isolation));
  OB_UNIS_VERSION(1);
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_TX_OPTIONS_H_
