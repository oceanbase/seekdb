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

#ifndef OB_FTS_STOP_WORD_H_
#define OB_FTS_STOP_WORD_H_

#include "lib/container/ob_iarray.h"
#include "lib/hash/ob_hashset.h"
#include "object/ob_object.h"
#include "data_plane/fts/ob_fts_struct.h"

namespace oceanbase
{
namespace storage
{

class ObFTParserProperty;

#define FTS_STOP_WORD_MAX_LENGTH 10

static const char ob_stop_word_list[][FTS_STOP_WORD_MAX_LENGTH] = {
  "a",
  "about",
  "an",
  "are",
  "as",
  "at",
  "be",
  "by",
  "com",
  "de",
  "en",
  "for",
  "from",
  "how",
  "i",
  "in",
  "is",
  "it",
  "la",
  "of",
  "on",
  "or",
  "that",
  "the",
  "this",
  "to",
  "was",
  "what",
  "when",
  "where",
  "who",
  "will",
  "with",
  "und",
  "the",
  "www"
};

class ObStopWordChecker final
{
public:
  ObStopWordChecker() = default;
  ~ObStopWordChecker();

  int init();
  void destroy();
  int check_stopword(const ObFTWord &word, bool &is_stopword);

private:
  static const int64_t DEFAULT_STOPWORD_BUCKET_NUM = 37L;
  typedef common::hash::ObHashSet<storage::ObFTWord> StopWordSet;

  StopWordSet stopword_set_;
  ObObjMeta stopword_type_;

  bool inited_ = false;

  static_assert(sizeof(ob_stop_word_list) / sizeof(ob_stop_word_list[0]) <= DEFAULT_STOPWORD_BUCKET_NUM,
              "ob_stop_word_list's number shouldn't be greater than DEFAULT_STOPWORD_BUCKET_NUM");
};

// ObAddWord is templated on the word map type so the per-call tokenize path
// can run on the latch-free ObFTWordMap while the query-side callers keep
// feeding their own ObFTWordMapLocked instances into
// ObFTParseHelper::segment(). The method bodies are defined in
// ob_fts_stop_word.cpp with explicit instantiations for both map flavors.
template <class MapType>
class ObAddWordT final
{
public:
  ObAddWordT(
      const ObFTParserProperty &property,
      const ObObjMeta &meta,
      const ObAddWordFlag &flag,
      common::ObIAllocator &allocator,
      MapType &word_map);
  ~ObAddWordT() = default;
  int process_word(
      const char *word,
      const int64_t word_len,
      const int64_t char_cnt,
      const int64_t word_freq);
  virtual int64_t get_add_word_count() const { return non_stopword_cnt_; }
  VIRTUAL_TO_STRING_KV(
      K_(word_meta),
      K_(min_max_word_cnt),
      K_(non_stopword_cnt),
      K_(stopword_cnt),
      K_(min_token_size),
      K_(max_token_size),
      K(word_map_.size()));

private:
  bool is_min_max_word(const int64_t c_len) const;
  int casedown_word(const ObFTWord &src, ObFTWord &dst);
  int check_stopword(const ObFTWord &word, bool &is_stopword);
  int groupby_word(const ObFTWord &word, const int64_t word_cnt);
  // copy the word bytes into the parse allocator so a map key outlives the
  // parser-owned buffer it was produced in
  int dup_word(const ObFTWord &src, ObFTWord &dst);
private:
  ObObjMeta word_meta_;
  common::ObIAllocator &allocator_;
  MapType &word_map_;
  int64_t min_max_word_cnt_;
  int64_t non_stopword_cnt_;
  int64_t stopword_cnt_;
  int64_t min_token_size_;
  int64_t max_token_size_;
  ObAddWordFlag flag_;
};

using ObAddWord = ObAddWordT<ObFTWordMap>;

} // end namespace storage
} // end namespace oceanbase

#endif // OB_FTS_STOP_WORD_H_
