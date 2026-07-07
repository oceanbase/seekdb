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

#ifndef _OCEANBASE_STORAGE_FTS_IK_OB_IK_CJK_PROCESSOR_H_
#define _OCEANBASE_STORAGE_FTS_IK_OB_IK_CJK_PROCESSOR_H_

#include "lib/allocator/ob_allocator.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/ik/ob_ik_processor.h"

namespace oceanbase
{
namespace storage
{
class ObFTDictHub;
class ObIKCJKProcessor : public ObIIKProcessor
{
public:
  // seekdb: dict_custom may be nullptr (no custom dict configured for this index).
  ObIKCJKProcessor(const ObIFTDict &dict_main, const ObIFTDict *dict_custom, ObIAllocator &alloc)
      : hits_(alloc), custom_hits_(alloc), dict_main_(dict_main), dict_custom_(dict_custom),
        cjk_start_(-1), cjk_end_(-1)
  {
  }
  ~ObIKCJKProcessor() override
  {
    hits_.reset();
    custom_hits_.reset();
  }

public:
  int do_process(TokenizeContext &ctx,
                 const char *ch,
                 const uint8_t char_len,
                 const ObFTCharUtil::CharType type) override;

private:
  // seekdb: run the IK CJK matching for one dictionary against its own in-progress hit list,
  // emitting matched lexemes into ctx. Used for both the built-in main dict and the custom dict.
  int do_match(TokenizeContext &ctx,
               const char *ch,
               const uint8_t char_len,
               const ObIFTDict &dict,
               ObList<ObDATrieHit, ObIAllocator> &hits);

private:
  ObList<ObDATrieHit, ObIAllocator> hits_;
  ObList<ObDATrieHit, ObIAllocator> custom_hits_;
  const ObIFTDict &dict_main_;
  const ObIFTDict *dict_custom_;
  int64_t cjk_start_;
  int64_t cjk_end_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObIKCJKProcessor);
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_IK_OB_IK_CJK_PROCESSOR_H_
