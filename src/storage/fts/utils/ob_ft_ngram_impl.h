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

#ifndef _OCEANBASE_STORAGE_FTS_UTILS_OB_FT_NGRAM_IMPL_H_
#define _OCEANBASE_STORAGE_FTS_UTILS_OB_FT_NGRAM_IMPL_H_

#include "lib/charset/ob_ctype.h"
#include "storage/fts/ob_fts_literal.h"

#include <cstdint>

namespace oceanbase
{
namespace storage
{
// ngram parser for both ngram and range ngram
class ObFTNgramImpl final
{
public:
  ObFTNgramImpl();
  ~ObFTNgramImpl();

  int init(const ObCharsetInfo *const cs,
           const char *const fulltext,
           const int64_t fulltext_len,
           const int64_t min,
           const int64_t max);

  void reset();

  int get_next_token(const char *&word, int64_t &word_len, int64_t &char_cnt, int64_t &word_freq);

private:
  static constexpr int64_t NGRAM_ARRAY_SIZE = ObFTSLiteral::FT_NGRAM_MAX_TOKEN_SIZE_UPPER_BOUND + 4;
  struct Word
  {
    const char *ptr;
    int64_t len;
  };

  struct Window
  {
    Word word_[NGRAM_ARRAY_SIZE];
    int64_t min_ngram_size_;
    int64_t max_ngram_size_;
    int start_;
    int cnt_;
    int ngram_n_;
    bool meet_delimiter_;
    bool is_last_batch_;

    void reset()
    {
      start_ = 0;
      cnt_ = 0;
      ngram_n_ = 0;
      meet_delimiter_ = false;
      is_last_batch_ = false;
    }

    void add_word(const Word &word);

    void out_ngram(const char *&word, int64_t &word_len, int64_t &char_cnt, int64_t &word_freq);

    void pop_start()
    {
      start_ = (start_ + 1) % NGRAM_ARRAY_SIZE;
      cnt_--;
    }
  };

  int next();

private:
  const ObCharsetInfo *cs_;
  const char *fulltext_start_;
  const char *fulltext_end_;
  const char *cur_;
  Window window_;

  bool is_inited_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_UTILS_OB_FT_NGRAM_IMPL_H_
