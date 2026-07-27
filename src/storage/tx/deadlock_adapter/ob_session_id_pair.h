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

#ifndef STROAGE_TX_DEADLOCK_ADAPTER_OB_SESSION_ID_PAIR_H
#define STROAGE_TX_DEADLOCK_ADAPTER_OB_SESSION_ID_PAIR_H
#include "lib/ob_errno.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace transaction
{

struct SessionIDPair {
  OB_UNIS_VERSION(1);
public:
  SessionIDPair() : sess_id_(0) {}
  explicit SessionIDPair(const uint32_t sess_id) : sess_id_(sess_id) {}
  uint32_t get_valid_sess_id() const {
    if (sess_id_ == 0) {
      DETECT_LOG_RET(WARN, OB_ERR_UNEXPECTED, "get_valid_sess_id is 0", K(*this));
    }
    return sess_id_;
  }
  bool is_valid() const {
    return sess_id_ != 0;
  }
  TO_STRING_KV(K_(sess_id));
  uint32_t sess_id_;
};
OB_SERIALIZE_MEMBER_TEMP(inline, SessionIDPair, sess_id_);

}
}
#endif
