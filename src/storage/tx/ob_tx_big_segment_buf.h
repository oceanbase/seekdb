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

#ifndef OCEANBASE_TRANSACTION_OB_TX_BIG_SEGMENT_BUF
#define OCEANBASE_TRANSACTION_OB_TX_BIG_SEGMENT_BUF

#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace transaction
{
static const uint64_t INVALID_SEGMENT_PART_ID = UINT64_MAX;

struct BigSegmentPartHeader
{
  uint64_t prev_part_id_;
  int64_t remain_length_;
  int64_t part_length_;

  void reset()
  {
    prev_part_id_ = INVALID_SEGMENT_PART_ID;
    remain_length_ = -1;
    part_length_ = -1;
  }

  bool is_first_part() { return prev_part_id_ == INVALID_SEGMENT_PART_ID; }

  BigSegmentPartHeader() { reset(); }

  TO_STRING_KV(K(prev_part_id_), K(remain_length_), K(part_length_));

  OB_UNIS_VERSION(1);
};

class ObTxBigSegmentBuf
{
public:
  ObTxBigSegmentBuf() : segment_buf_(nullptr) { reset(); }
  void reset();

public:
  int init_for_serialize(int64_t segment_len);
  template <typename T>
  int serialize_object(const T &obj);
  template <typename T>
  int deserialize_object(T &obj);


  /**
   * OB_ITER_END : no part can be split or collect
   * */
  int split_one_part(char *part_buf,
                     const int64_t part_buf_len,
                     int64_t &part_buf_pos,
                     bool &need_fill_id_into_next_part);
  int collect_one_part(const char *part_buf, const int64_t part_buf_len, int64_t &part_buf_pos);


  bool is_completed();
  bool is_inited();
  bool is_active();

  int set_prev_part_id(const uint64_t prev_part_id);

  TO_STRING_KV(KP(segment_buf_),
               K(segment_buf_len_),
               K(segment_data_len_),
               K(segment_pos_),
               K(is_complete_),
               K(for_serialize_),
               K(prev_part_id_));

private:
  int basic_init_(const int64_t segment_len, bool for_serialize);

private:
  char *segment_buf_;
  int64_t segment_buf_len_;
  int64_t segment_data_len_;
  int64_t segment_pos_;
  bool is_complete_;
  bool for_serialize_;

  uint64_t prev_part_id_;
};

template <typename T>
int ObTxBigSegmentBuf::serialize_object(const T &obj)
{
  int ret = OB_SUCCESS;

  int64_t tmp_pos = segment_data_len_;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "The big segment buf is not inited", K(ret));
  } else if (OB_FAIL(obj.serialize(segment_buf_, segment_buf_len_, tmp_pos))) {
    TRANS_LOG(WARN, "serialize object in big segment failed", K(ret));
  } else {
    segment_data_len_ = tmp_pos;
  }
  return ret;
}

template <typename T>
int ObTxBigSegmentBuf::deserialize_object(T &obj)
{
  int ret = OB_SUCCESS;

  int64_t tmp_pos = segment_pos_;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "The big segment buf is not inited", K(ret));
  } else if (OB_FAIL(obj.deserialize(segment_buf_, segment_buf_len_, tmp_pos))) {
    TRANS_LOG(WARN, "serialize object in big segment failed", K(ret));
  } else {
    segment_pos_ = tmp_pos;
  }
  return ret;
}

} // namespace transaction

} // namespace oceanbase
#endif
