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

#ifndef OCEANBASE_SHARE_OB_TOKEN_STREAM_H_
#define OCEANBASE_SHARE_OB_TOKEN_STREAM_H_

#include "share/datum/ob_datum.h"
namespace oceanbase
{
namespace share
{

class ObITokenStream
{
public:
  ObITokenStream() {}
  virtual ~ObITokenStream() {}
  virtual void reset() = 0;
  virtual void reuse() = 0;
  virtual int get_next(ObDatum &next_token, int64_t &token_freq) = 0;
  virtual int get_next(ObDatum &next_token)
  {
    int64_t token_freq = 0;
    return get_next(next_token, token_freq);
  }

  DECLARE_PURE_VIRTUAL_TO_STRING;
};

class ObTextTokenizer : public ObITokenStream
{
public:
  enum TokenizerType : uint8_t
  {
    WHITESPACE = 0,
    MAX
  };
  ObTextTokenizer();
  virtual ~ObTextTokenizer() {}
  int open(const ObDatum &document, const ObCharsetInfo *cs);
  virtual void reset() override;
  virtual void reuse() override { reset(); }
protected:
  virtual int inner_open(const ObDatum &doc, const ObCharsetInfo *cs) { return OB_NOT_IMPLEMENT; }
  VIRTUAL_TO_STRING_KV(KPC_(input_doc), KP_(cs), K_(iter_end), K_(is_inited));
protected:
  const ObDatum *input_doc_;
  const ObCharsetInfo *cs_;
  bool iter_end_;
  bool is_inited_;
};

// tokenize by whitespace and special marks
class ObTextWhitespaceTokenizer final : public ObTextTokenizer
{
public:
  ObTextWhitespaceTokenizer();
  virtual ~ObTextWhitespaceTokenizer() {}
  virtual int get_next(ObDatum &next_token, int64_t &token_freq) override;
  virtual void reset() override;
protected:
  virtual int inner_open(const ObDatum &document, const ObCharsetInfo *cs) override;
  INHERIT_TO_STRING_KV("ObTextTokenizer", ObTextTokenizer, K_(trav_pos), KP_(curr_token_ptr));
private:
  bool found_delimiter();
  const char *get_trav_ptr() { return input_doc_->ptr_ + trav_pos_; }
  uint32_t get_input_buf_len() { return input_doc_->len_; }
private:
  const char *curr_token_ptr_;
  int64_t trav_pos_;
};

class ObTokenNormalizer : public ObITokenStream
{
public:
  enum TokenNormalizerType : uint8_t
  {
    STOPWORD_FILTER = 0,
    TEXT_GROUPING_FILTER = 1,
    ENG_BASIC_NORM = 2,
    MAX
  };
  ObTokenNormalizer();
  virtual ~ObTokenNormalizer() {}
  virtual void reset();
  virtual void reuse() override;
  virtual int init(const ObCharsetInfo *cs, ObITokenStream &in_stream);
  VIRTUAL_TO_STRING_KV(KPC_(in_stream), KP_(cs), K_(is_inited));
protected:
  virtual int inner_init(const ObCharsetInfo *cs, ObITokenStream &in_stream) { return OB_SUCCESS; }
protected:
  ObITokenStream *in_stream_;
  const ObCharsetInfo *cs_;
  bool is_inited_;
};

// filter by punctuation mark, control mark or stop word dictionary
class ObTokenStopWordNormalizer final : public ObTokenNormalizer
{
public:
  ObTokenStopWordNormalizer() : ObTokenNormalizer() {}
  virtual ~ObTokenStopWordNormalizer() {}

  virtual int get_next(ObDatum &next_token, int64_t &token_freq) override;
private:
  int filter_special_marks(const ObDatum &check_token, bool &is_valid);
  // int check_stop_words(const ObDatum &check_token, bool &is_valid);
};

// remove leading / trailing punctuations and to_lower case alphabetic characters
class ObBasicEnglishNormalizer final : public ObTokenNormalizer
{
public:
  ObBasicEnglishNormalizer();
  virtual ~ObBasicEnglishNormalizer() {};

  virtual void reset() override;
  virtual void reuse() override;
  virtual int get_next(ObDatum &next_token, int64_t &token_freq) override;
private:
  ObArenaAllocator norm_allocator_;
};

// group (deduplicate) and count
class ObTextTokenGroupNormalizer final : public ObTokenNormalizer
{
public:
  ObTextTokenGroupNormalizer();
  virtual ~ObTextTokenGroupNormalizer() { reset(); }
  virtual void reset() override;
  virtual void reuse() override;
  // Do we need to keep the order of tokens after grouping?
  virtual int get_next(ObDatum &next_token, int64_t &token_freq) override;
private:
  virtual int inner_init(const ObCharsetInfo *cs, ObITokenStream &in_stream) override;
  int build_grouping_map();
private:
  static const int64_t DEFAULT_HASH_MAP_BUCKET_CNT = 128;
  ObArenaAllocator token_allocator_;
  common::hash::ObHashMap<ObString, int64_t> grouping_map_;
  common::hash::ObHashMap<ObString, int64_t>::const_iterator map_iter_;
  common::hash::ObHashMap<ObString, int64_t>::const_iterator map_end_iter_;
  bool in_stream_iter_end_;
};

}; // namespace share
}; // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_TOKEN_STREAM_H_
