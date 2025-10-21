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

#define USING_LOG_PREFIX STORAGE

#include "ob_column_encoding_struct.h"
#include "ob_cs_encoding_util.h"
#include "storage/blocksstable/cs_encoding/semistruct_encoding/ob_semistruct_encoding_util.h"


namespace oceanbase
{
namespace blocksstable
{
using namespace common;

const bool ObCSEncodingOpt::STREAM_ENCODINGS_DEFAULT[ObIntegerStream::EncodingType::MAX_TYPE]
  = {
       false, // MIN_TYPE
       true,  // RAW
       true,  // DOUBLE_DELTA_ZIGZAG_RLE
       true,  // DOUBLE_DELTA_ZIGZAG_PFOR
       true,  // DELTA_ZIGZAG_RLE
       true,  // DELTA_ZIGZAG_PFOR
       true,  // SIMD_FIXEDPFOR
       true,  // UNIVERSAL_COMPRESS
       true  // XOR_FIXED_PFOR
      };
const bool ObCSEncodingOpt::STREAM_ENCODINGS_NONE[ObIntegerStream::EncodingType::MAX_TYPE]
    = {false, false, false, false, false, false, false, false, false};

int ObPreviousCSEncoding::init(const int32_t col_count)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_FAIL(previous_encoding_of_columns_.prepare_allocate(col_count))) {
    LOG_WARN("fail to prepare_allocate", K(ret), K(col_count));
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObPreviousCSEncoding::reset()
{
  previous_encoding_of_columns_.reset();
  is_inited_ = false;
}

int ObPreviousCSEncoding::update_column_detect_info(const int32_t column_idx,
                                                 const ObColumnEncodingIdentifier identifier,
                                                 const int64_t cur_block_count,
                                                 const int64_t major_working_cluster_version)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else {
    ObPreviousColumnEncoding &previous = previous_encoding_of_columns_.at(column_idx);
    if (previous.is_stream_encoding_type_valid_) {
      if (previous.identifier_ != identifier) {
        previous.is_stream_encoding_type_valid_ = false;
      } else {
        if (previous.force_no_redetect_) {
          previous.stream_need_redetect_ = false;
        } else if (0 == cur_block_count % previous.stream_redetect_cycle_) {
          previous.stream_need_redetect_ = true;
        } else {
          previous.stream_need_redetect_ = false;
        }
      }
    }

    const int32_t max_redect_cycle = 128;
    const int32_t min_redect_cycle = 16;

    if (previous.is_column_encoding_type_valid()) {
      if (previous.force_no_redetect_) {
        previous.column_need_redetect_ = false;
      } else if (previous.identifier_.column_encoding_type_ == identifier.column_encoding_type_) {
        if (previous.column_need_redetect_) { // has done redection and column encoding type not changed
          previous.column_need_redetect_ = false;
          previous.column_redetect_cycle_ = previous.column_redetect_cycle_ << 1;
          previous.column_redetect_cycle_ = std::min(previous.column_redetect_cycle_, max_redect_cycle);
        } else if (0 == cur_block_count % previous.column_redetect_cycle_) {
          previous.column_need_redetect_ = true;
        }
      } else {
        previous.identifier_.column_encoding_type_ = identifier.column_encoding_type_;
        previous.column_redetect_cycle_ = min_redect_cycle;
      }
    } else {
      previous.identifier_.column_encoding_type_ = identifier.column_encoding_type_;
      previous.column_redetect_cycle_ = min_redect_cycle;
    }

    previous.column_idx_ = column_idx;
    previous.cur_block_count_ = cur_block_count;
  }
  return ret;
}


int ObPreviousCSEncoding::update_stream_detect_info(
                        const int32_t column_idx,
                        const ObColumnEncodingIdentifier identifier,
                        const ObIntegerStream::EncodingType *stream_types,
                        const int64_t major_working_cluster_version,
                        bool force_no_redetect)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(stream_types == nullptr && identifier.int_stream_count_ != 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argumnet", KR(ret), KP(stream_types), K(identifier));
  } else {
    int32_t max_redect_cycle = 128;
    int32_t min_redect_cycle = 16;

    ObPreviousColumnEncoding &previous = previous_encoding_of_columns_.at(column_idx);
    if (previous.is_stream_encoding_type_valid_ && previous.identifier_ != identifier) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("identifier must be same", KR(ret), K(column_idx), K(identifier), K(previous));
    } else if (previous.is_stream_encoding_type_valid_) {
      if (!previous.stream_need_redetect_) {
        // Previous stream encoding is valid and no redetection occurs, which represent the previous
        // encoding has been reused. There is no need to increase the redect_cycle. We fix this case
        // in OB435bp1.
      } else {
        bool stream_encoding_type_not_change = true;
        for (int32_t i = 0; i < identifier.int_stream_count_; i++) {
          if (previous.stream_encoding_types_[i] != stream_types[i]) {
            stream_encoding_type_not_change = false;
            break;
          }
        }
        if (stream_encoding_type_not_change) {
          previous.stream_redetect_cycle_ = previous.stream_redetect_cycle_ << 1;
          previous.stream_redetect_cycle_ = std::min(previous.stream_redetect_cycle_, max_redect_cycle);
        } else {
          for (int32_t i = 0; i < identifier.int_stream_count_; i++) {
            previous.stream_encoding_types_[i] = stream_types[i];
          }
          previous.stream_redetect_cycle_ = min_redect_cycle;
        }
      }
      previous.force_no_redetect_ = force_no_redetect;
    } else {
      // column_encoding_type_ has been update in update_column_detect_info, so must be equal here
      //if (!force_no_redetect) {
      //  OB_ASSERT(previous.identifier_.column_encoding_type_ == identifier.column_encoding_type_);
      //}
      previous.identifier_ = identifier;
      for (int32_t i = 0; i < identifier.int_stream_count_; i++) {
        previous.stream_encoding_types_[i] = stream_types[i];
      }
      previous.is_stream_encoding_type_valid_ = true;
      previous.stream_redetect_cycle_ = min_redect_cycle;
      previous.force_no_redetect_ = force_no_redetect;
    }
  }

  return ret;
}

void ObColumnCSEncodingCtx::try_set_need_sort(const ObCSColumnHeader::Type type,
                                              const int64_t column_index,
                                              const bool micro_block_has_lob_out_row,
                                              const int64_t major_working_cluster_version)
{
  try_set_need_sort(type, encoding_ctx_->col_descs_->at(column_index).col_type_.get_type_class(), micro_block_has_lob_out_row, major_working_cluster_version);
}

void ObColumnCSEncodingCtx::try_set_need_sort(const ObCSColumnHeader::Type type,
                                              const ObObjTypeClass col_tc,
                                              const bool micro_block_has_lob_out_row,
                                              const int64_t major_working_cluster_version)
{
  need_sort_ = (type == ObCSColumnHeader::INT_DICT);
}

}
}
