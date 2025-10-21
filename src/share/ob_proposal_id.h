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

#ifndef OCEANBASE_COMMON_OB_PROPOSAL_ID_
#define OCEANBASE_COMMON_OB_PROPOSAL_ID_

#include "lib/net/ob_addr.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{
class ObProposalID
{
public:
  const static int8_t PROPOSAL_ID_VERSION = 4;
  const static int8_t PROPOSAL_ID_VERSION6 = 6;

  ObProposalID() : addr_(), ts_(OB_INVALID_TIMESTAMP) {}

  bool operator < (const ObProposalID &pid) const;
  bool operator > (const ObProposalID &pid) const;
  bool operator == (const ObProposalID &pid) const;

  ObAddr addr_;
  int64_t ts_;

  TO_STRING_KV(N_TIME_TO_USEC, ts_, N_SERVER, addr_);
  NEED_SERIALIZE_AND_DESERIALIZE;
};

}//end namespace common
}//end namespace oceanbase
#endif //OCEANBASE_COMMON_OB_PROPOSAL_ID_
