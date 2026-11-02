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

#ifndef OCEABASE_LOGSERVICE_LSN_
#define OCEABASE_LOGSERVICE_LSN_
#include "lib/ob_define.h"                      // Serialization
#include "lib/utility/ob_macro_utils.h"         // OB_UNLIKELY
#include "lib/utility/ob_print_utils.h"         // Print*
#include "lib/json/ob_yson.h"
#include "log_define.h"                         // block_id_t
namespace oceanbase {
namespace palf {
struct LSN
{
  LSN();
  explicit LSN(const offset_t offset);
  LSN(const LSN & lsn);
  ~LSN() {}

  bool is_valid() const;
  void reset();

  friend LSN operator+(const LSN &lsn, const offset_t len);
  friend LSN operator-(const LSN &lsn, const offset_t len);
  friend offset_t operator-(const LSN &lhs, const LSN &rhs);
  friend bool operator==(const uint64_t offset, const LSN &lsn);
  bool operator==(const LSN &lsn) const;
  bool operator!=(const LSN &lsn) const;
  bool operator<(const LSN &lsn) const;
  bool operator>(const LSN &lsn) const;
  bool operator>=(const LSN &lsn) const;
  bool operator<=(const LSN &lsn) const;
  LSN& operator=(const LSN &lsn);
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_AND_YSON(OB_ID(lsn), val_);
  offset_t val_;
};

struct LSNCompare final
{
public:
  LSNCompare() {}
  ~LSNCompare() {}
  bool operator() (const LSN &left, const LSN &right)
  {
    return (left > right);
  }
};

inline block_id_t lsn_2_block(const LSN &lsn, const uint64_t block_size)
{
  return lsn.val_ / block_size;
}

inline offset_t lsn_2_offset(const LSN &lsn, const uint64_t block_size)
{
  return lsn.val_ % block_size;
}
} // end namespace palf
} // end namespace oceanbase

#endif
