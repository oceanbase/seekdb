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

#ifndef OB_FULLTEXT_INDEX_WRITE_PIPELINE_H_
#define OB_FULLTEXT_INDEX_WRITE_PIPELINE_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_errno.h"
#include "share/task/ob_dag.h"

namespace oceanbase
{
namespace storage
{

class ObFtsSortFlushOperator
{
public:
  ObFtsSortFlushOperator() : is_inited_(false) {}

  int init(ObIAllocator &allocator)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    is_inited_ = true;
    return ret;
  }

  int flush(int64_t doc_id, int64_t word_id, int64_t word_count, const common::ObString &pos_list)
  {
    int ret = OB_SUCCESS;
    return ret;
  }

private:
  bool is_inited_;
  ObIAllocator *allocator_;
};

class ObFtsMacroBlockWriteOperator
{
public:
  ObFtsMacroBlockWriteOperator() : is_inited_(false) {}

  int init()
  {
    int ret = OB_SUCCESS;
    is_inited_ = true;
    return ret;
  }

  int write_macro_block()
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  int close()
  {
    int ret = OB_SUCCESS;
    return ret;
  }

private:
  bool is_inited_;
};

class ObFullTextIndexWritePipeline : public share::ObITask
{
public:
  ObFullTextIndexWritePipeline()
    : is_inited_(false), sort_flush_op_(), macro_write_op_()
  {
  }

  int init(ObIAllocator &allocator)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(sort_flush_op_.init(allocator))) {
    } else if (OB_FAIL(macro_write_op_.init())) {
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
  ObFtsSortFlushOperator sort_flush_op_;
  ObFtsMacroBlockWriteOperator macro_write_op_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_FULLTEXT_INDEX_WRITE_PIPELINE_H_ */
