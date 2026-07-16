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

#ifndef OB_FTS_SAMPLE_PIPELINE_H_
#define OB_FTS_SAMPLE_PIPELINE_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_errno.h"
#include "share/task/ob_dag.h"

namespace oceanbase
{
namespace storage
{

class ObFtsForwardInvertSampleOperator
{
public:
  ObFtsForwardInvertSampleOperator() : is_inited_(false), sample_count_(0) {}

  int init(ObIAllocator &allocator, int64_t sample_count)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    sample_count_ = sample_count;
    is_inited_ = true;
    return ret;
  }

  int add_sample_row(const common::ObString &range_key, int64_t doc_id)
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int compute_boundaries()
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  OB_INLINE int64_t get_sample_count() const { return sample_count_; }

private:
  bool is_inited_;
  int64_t sample_count_;
  ObIAllocator *allocator_;
};

class ObFtsWriteInnerTableOperator
{
public:
  ObFtsWriteInnerTableOperator() : is_inited_(false) {}

  int init()
  {
    int ret = OB_SUCCESS;
    is_inited_ = true;
    return ret;
  }

  int persist_boundaries()
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int load_boundaries()
  {
    int ret = OB_SUCCESS;
    return ret;
  }

private:
  bool is_inited_;
};

class ObFtsSamplePipeline : public share::ObITask
{
public:
  ObFtsSamplePipeline()
    : is_inited_(false), sample_op_(), write_op_()
  {
  }

  int init(ObIAllocator &allocator, int64_t sample_count)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    if (OB_FAIL(sample_op_.init(allocator, sample_count))) {
    } else if (OB_FAIL(write_op_.init())) {
    } else {
      is_inited_ = true;
    }
    return ret;
  }

  int process() override
  {
    int ret = OB_SUCCESS;
    return ret;
  }

private:
  bool is_inited_;
  ObIAllocator *allocator_;
  ObFtsForwardInvertSampleOperator sample_op_;
  ObFtsWriteInnerTableOperator write_op_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_FTS_SAMPLE_PIPELINE_H_ */
