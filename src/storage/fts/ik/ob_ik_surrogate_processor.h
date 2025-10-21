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

#ifndef _OCEANBASE_STORAGE_FTS_IK_OB_IK_SURROGATE_PROCESSOR_H_
#define _OCEANBASE_STORAGE_FTS_IK_OB_IK_SURROGATE_PROCESSOR_H_

#include "storage/fts/ik/ob_ik_processor.h"

namespace oceanbase
{
namespace storage
{
// Only for UTF16
class ObIKSurrogateProcessor : public ObIIKProcessor
{
public:
  ObIKSurrogateProcessor() {}
  ~ObIKSurrogateProcessor() override {}

  int do_process(TokenizeContext &ctx,
                 const char *ch,
                 const uint8_t char_len,
                 const ObFTCharUtil::CharType type) override;

private:
  void reset()
  {
    low_offset_ = -1;
    high_offset_ = -1;
  }

  bool has_high() { return high_offset_ != -1; }

private:
  int64_t high_offset_ = -1;
  int64_t low_offset_ = -1;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_IK_OB_IK_SURROGATE_PROCESSOR_H_
