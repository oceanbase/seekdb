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

#include "share/ob_proposal_id.h"

#include "deps/oblib/src/lib/hash/ob_hashmap.h"

namespace oceanbase
{
namespace common
{



bool ObProposalID::operator < (const ObProposalID &pid) const
{
  return (ts_ == OB_INVALID_TIMESTAMP && pid.ts_ != OB_INVALID_TIMESTAMP)
      || (ts_ < pid.ts_)
      || ((ts_ == pid.ts_) && (addr_ < pid.addr_));
}

bool ObProposalID::operator > (const ObProposalID &pid) const
{
  return (ts_ != OB_INVALID_TIMESTAMP && pid.ts_ == OB_INVALID_TIMESTAMP)
      || (ts_ > pid.ts_)
      || ((ts_ == pid.ts_) && (pid.addr_ < addr_));
}




bool ObProposalID::operator == (const ObProposalID &pid) const
{
  return (ts_ == pid.ts_) && (addr_ == pid.addr_);
}


DEFINE_SERIALIZE(ObProposalID)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  int8_t current_version = addr_.using_ipv4() ? PROPOSAL_ID_VERSION : PROPOSAL_ID_VERSION6;

  if ((NULL == buf) || (buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((OB_FAIL(serialization::encode_i8(buf, buf_len, new_pos, current_version)))) {
    CLOG_LOG(WARN, "serialize version error", K(ret), K(buf), K(buf_len), K(pos), K(new_pos));
  }

  if (OB_SUCC(ret)) {
    if (PROPOSAL_ID_VERSION6 == current_version) {
      if ((OB_FAIL(addr_.serialize(buf, buf_len, new_pos)))) {
        CLOG_LOG(WARN, "serialize addr error", K(ret), K(buf), K(buf_len), K(pos), K(new_pos));
      }
    } else {
      if ((OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos,
                                             addr_.get_ipv4_server_id())))) {
        CLOG_LOG(WARN, "serialize addr error", K(ret), K(buf), K(buf_len), K(pos), K(new_pos));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if ((OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, ts_)))) {
    CLOG_LOG(WARN, "serialize timestamp error", K(ret), K(buf), K(buf_len), K(pos), K(new_pos));
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_DESERIALIZE(ObProposalID)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  int64_t server_id = 0;
  int8_t current_version = 0;

  if (OB_ISNULL(buf) || (data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((OB_FAIL(serialization::decode_i8(buf, data_len, new_pos, &current_version)))) {
    // CLOG_LOG(WARN, "deserialize VERSION error", K(ret), K(buf), K(data_len), K(pos), K(new_pos));
  }

  if (OB_SUCC(ret)) {
    if (PROPOSAL_ID_VERSION6 == current_version) {
      ret = addr_.deserialize(buf, data_len, new_pos);
    } else {
      ret = serialization::decode_i64(buf, data_len, new_pos, &server_id);
      if (OB_SUCC(ret)) {
        addr_.set_ipv4_server_id(server_id);
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if ((OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, &ts_)))) {
    // CLOG_LOG(WARN, "deserialize timestamp error", K(ret), K(buf), K(data_len), K(pos), K(new_pos));
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObProposalID)
{
  int64_t size = 0;
  int8_t current_version = addr_.using_ipv4() ? PROPOSAL_ID_VERSION : PROPOSAL_ID_VERSION6;
  size += serialization::encoded_length_i8(current_version);
  size += serialization::encoded_length_i64(ts_);
  if (PROPOSAL_ID_VERSION6 == current_version) {
    size += addr_.get_serialize_size();
  } else {
    size += serialization::encoded_length_i64(addr_.get_ipv4_server_id());
  }
  return size;
}

}//end namespace common
}//end namespace oceanbase
