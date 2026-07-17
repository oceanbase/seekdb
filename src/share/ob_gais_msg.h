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

#ifndef _OB_SHARE_OB_GAIS_MSG_H_
#define _OB_SHARE_OB_GAIS_MSG_H_

#include "share/ob_autoincrement_param.h"
#include "share/ob_define.h"
#include "share/sequence/ob_sequence_option.h"

namespace oceanbase
{
namespace obcall
{
struct ObGAISNextValResult
{
  ObGAISNextValResult() : start_inclusive_(0), end_inclusive_(0), sync_value_(0) {}
  int init(const uint64_t start_inclusive, const uint64_t end_inclusive, const uint64_t sync_value);
  bool is_valid() const
  {
    return start_inclusive_ > 0 && end_inclusive_ > 0 && start_inclusive_ <= end_inclusive_
             && sync_value_ <= end_inclusive_;
  }
  TO_STRING_KV(K_(start_inclusive), K_(end_inclusive), K_(sync_value));

  uint64_t start_inclusive_;
  uint64_t end_inclusive_;
  uint64_t sync_value_;
};

struct ObGAISCurrValResult
{
  ObGAISCurrValResult() : sequence_value_(0), sync_value_(0) {}
  int init(const uint64_t sequence_value, const uint64_t sync_value);
  bool is_valid() const
  {
    return sequence_value_ > 0 && sequence_value_ >= sync_value_;
  }
  void reset()
  {
    sequence_value_ = 0;
    sync_value_ = 0;
  }
  TO_STRING_KV(K_(sequence_value), K_(sync_value));

  uint64_t sequence_value_;
  uint64_t sync_value_;
};

struct ObGAISNextSequenceValResult
{
  ObGAISNextSequenceValResult() : nextval_() {}
  TO_STRING_KV(K_(nextval));
  share::ObSequenceValue nextval_;
};
}
namespace share
{

/* Request for get next auto increment value */
struct ObGAISNextAutoIncValReq
{
public:
  ObGAISNextAutoIncValReq() : autoinc_key_(), offset_(0), increment_(0), base_value_(0),
                              max_value_(0), desired_cnt_(0), cache_size_(0), autoinc_version_(OB_INVALID_VERSION) {}
  int init(const AutoincKey &autoinc_key,
           const uint64_t offset,
           const uint64_t increment,
           const uint64_t base_value,
           const uint64_t max_value,
           const uint64_t desired_cnt,
           const uint64_t cache_size,
           const int64_t &autoinc_version);
  bool is_valid() const
  {
    return offset_ > 0 && increment_ > 0 &&
             max_value_ > 0 && desired_cnt_ > 0 && cache_size_ > 0
             && autoinc_version_ >= OB_INVALID_VERSION;
  }
  TO_STRING_KV(K_(autoinc_key), K_(offset), K_(increment), K_(base_value), K_(max_value),
                                K_(desired_cnt), K_(cache_size), K_(autoinc_version));

  AutoincKey autoinc_key_;
  uint64_t offset_;
  uint64_t increment_;
  uint64_t base_value_;
  uint64_t max_value_;
  uint64_t desired_cnt_;
  uint64_t cache_size_;
  int64_t autoinc_version_;
};

/* GAIS autoinc key rpc argument */
struct ObGAISAutoIncKeyArg
{
public:
  ObGAISAutoIncKeyArg() : autoinc_key_(), autoinc_version_(OB_INVALID_VERSION) {}
  int init(const AutoincKey &autoinc_key, const int64_t autoinc_version);
  bool is_valid() const
  {
    return autoinc_version_ >= OB_INVALID_VERSION;
  }
  TO_STRING_KV(K_(autoinc_key), K_(autoinc_version));

  AutoincKey autoinc_key_;
  int64_t autoinc_version_;
};

/* Request for push local sync value to global */
struct ObGAISPushAutoIncValReq
{
public:
  ObGAISPushAutoIncValReq() : autoinc_key_(), base_value_(0), max_value_(0),
                              autoinc_version_(OB_INVALID_VERSION), cache_size_(0) {}
  int init(const AutoincKey &autoinc_key,
           const uint64_t base_value,
           const uint64_t max_value,
           const int64_t &autoinc_version,
           const int64_t cache_size);
  bool is_valid() const
  {
    return max_value_ > 0 && base_value_ <= max_value_
            && autoinc_version_ >= OB_INVALID_VERSION && cache_size_ >= 0;
  }
  TO_STRING_KV(K_(autoinc_key), K_(base_value), K_(max_value), K_(autoinc_version),
               K_(cache_size));

  AutoincKey autoinc_key_;
  uint64_t base_value_;
  uint64_t max_value_;
  int64_t autoinc_version_;
  int64_t cache_size_;
};

/* Request for get next sequence value */
struct ObGAISNextSequenceValReq
{
public:
  ObGAISNextSequenceValReq() : schema_() {}
  int init(const schema::ObSequenceSchema &schema);
  bool is_valid() const
  {
    return schema_.get_sequence_id() != OB_INVALID_ID
           && schema_.get_cache_size() > static_cast<int64_t>(0);
  }
  TO_STRING_KV(K_(schema));

  schema::ObSequenceSchema schema_;
};

} // share
} // oceanbase

#endif // _OB_SHARE_OB_GAIS_MSG_H_
