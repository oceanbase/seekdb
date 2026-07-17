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

#ifndef _OCEANBASE_STORAGE_FTS_IK_OB_IK_LETTER_PROCESSOR_H_
#define _OCEANBASE_STORAGE_FTS_IK_OB_IK_LETTER_PROCESSOR_H_

#include "storage/fts/ik/ob_ik_processor.h"

namespace oceanbase
{
namespace storage
{
class ObIKLetterProcessor : public ObIIKProcessor
{
public:
  ObIKLetterProcessor();
  ~ObIKLetterProcessor() override {}

  int do_process(TokenizeContext &ctx,
                 const char *ch,
                 const uint8_t char_len,
                 const ObFTCharUtil::CharType type) override;

private:
  int process_english_letter(TokenizeContext &ctx,
                             const char *ch,
                             const uint8_t char_len,
                             const ObFTCharUtil::CharType type);

  int process_arabic_letter(TokenizeContext &ctx,
                            const char *ch,
                            const uint8_t char_len,
                            const ObFTCharUtil::CharType type);

  int process_mix_letter(TokenizeContext &ctx,
                         const char *ch,
                         const uint8_t char_len,
                         const ObFTCharUtil::CharType type);

  void reset_english_state();

  void reset_arabic_state();

  void reset_mix_state();

private:
  int64_t english_start_ = -1;
  int64_t english_end_ = -1;
  int64_t english_char_cnt_ = 0;

  int64_t arabic_start_ = -1;
  int64_t arabic_end_ = -1;
  int64_t arabic_char_cnt_ = 0;
  int64_t arabic_connect_cnt_ = 0;

  int64_t mix_start_ = -1;
  int64_t mix_end_ = -1;
  int64_t mix_char_cnt_ = 0;
};
} // namespace storage
} // namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_IK_OB_IK_LETTER_PROCESSOR_H_
