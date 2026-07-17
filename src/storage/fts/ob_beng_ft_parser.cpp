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

#define USING_LOG_PREFIX STORAGE_FTS

#include "lib/utility/ob_target_specific.h"
#include "storage/fts/ob_beng_ft_parser.h"
#include "storage/fts/ob_fts_struct.h"
#if OB_USE_MULTITARGET_CODE
#include <immintrin.h>
#endif

using namespace oceanbase::common;
using namespace oceanbase::plugin;

namespace oceanbase
{
namespace storage
{

namespace
{
static constexpr uint64_t ASCII_HIGH_BIT_MASK64 = 0x8080808080808080ULL;

inline bool is_ascii_alnum_byte(const uint8_t c)
{
  return (c >= '0' && c <= '9')
      || (c >= 'A' && c <= 'Z')
      || (c >= 'a' && c <= 'z');
}

inline bool is_ascii_upper_byte(const uint8_t c)
{
  return c >= 'A' && c <= 'Z';
}

inline int64_t count_leading_delimiter_scalar(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos < len && !is_ascii_alnum_byte(static_cast<uint8_t>(str[pos])); ++pos) {
  }
  return pos;
}

inline int64_t count_leading_token_scalar(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos < len && is_ascii_alnum_byte(static_cast<uint8_t>(str[pos])); ++pos) {
  }
  return pos;
}

inline bool has_ascii_upper_scalar(const char *str, const int64_t len)
{
  bool found = false;
  for (int64_t i = 0; !found && i < len; ++i) {
    found = is_ascii_upper_byte(static_cast<uint8_t>(str[i]));
  }
  return found;
}

inline void lowercase_ascii_copy_scalar(const char *src, const int64_t len, char *dst)
{
  for (int64_t i = 0; i < len; ++i) {
    const uint8_t ch = static_cast<uint8_t>(src[i]);
    dst[i] = is_ascii_upper_byte(ch) ? static_cast<char>(ch + ('a' - 'A')) : src[i];
  }
}

inline bool is_ascii_document_scalar(const char *str, const int64_t len)
{
  bool is_ascii = true;
  int64_t pos = 0;
  for (; is_ascii && pos + static_cast<int64_t>(sizeof(uint64_t)) <= len; pos += sizeof(uint64_t)) {
    uint64_t chunk = 0;
    MEMCPY(&chunk, str + pos, sizeof(chunk));
    is_ascii = 0 == (chunk & ASCII_HIGH_BIT_MASK64);
  }
  for (; is_ascii && pos < len; ++pos) {
    is_ascii = 0 == (static_cast<uint8_t>(str[pos]) & 0x80);
  }
  return is_ascii;
}

OB_DECLARE_AVX2_SPECIFIC_CODE(

static constexpr int64_t AVX2_BYTES = sizeof(__m256i);

inline uint32_t get_ascii_alnum_mask(const char *str)
{
  const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(str));
  const __m256i zero = _mm256_set1_epi8('0' - 1);
  const __m256i nine = _mm256_set1_epi8('9' + 1);
  const __m256i upper_a = _mm256_set1_epi8('A' - 1);
  const __m256i upper_z = _mm256_set1_epi8('Z' + 1);
  const __m256i lower_a = _mm256_set1_epi8('a' - 1);
  const __m256i lower_z = _mm256_set1_epi8('z' + 1);
  const __m256i digits = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, zero), _mm256_cmpgt_epi8(nine, bytes));
  const __m256i uppers = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, upper_a), _mm256_cmpgt_epi8(upper_z, bytes));
  const __m256i lowers = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, lower_a), _mm256_cmpgt_epi8(lower_z, bytes));
  return static_cast<uint32_t>(
      _mm256_movemask_epi8(_mm256_or_si256(_mm256_or_si256(digits, uppers), lowers)));
}

inline uint32_t get_ascii_upper_mask(const char *str)
{
  const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(str));
  const __m256i upper_a = _mm256_set1_epi8('A' - 1);
  const __m256i upper_z = _mm256_set1_epi8('Z' + 1);
  const __m256i uppers = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, upper_a), _mm256_cmpgt_epi8(upper_z, bytes));
  return static_cast<uint32_t>(_mm256_movemask_epi8(uppers));
}

inline int64_t count_leading_delimiter_avx2(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    const uint32_t mask = get_ascii_alnum_mask(str + pos);
    if (0 != mask) {
      return pos + __builtin_ctz(mask);
    }
  }
  return pos + oceanbase::storage::count_leading_delimiter_scalar(str + pos, len - pos);
}

inline int64_t count_leading_token_avx2(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    const uint32_t mask = get_ascii_alnum_mask(str + pos);
    if (UINT32_MAX != mask) {
      return pos + __builtin_ctz(~mask);
    }
  }
  return pos + oceanbase::storage::count_leading_token_scalar(str + pos, len - pos);
}

inline bool has_ascii_upper_avx2(const char *str, const int64_t len)
{
  bool found = false;
  int64_t pos = 0;
  for (; !found && pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    found = 0 != get_ascii_upper_mask(str + pos);
  }
  return found || oceanbase::storage::has_ascii_upper_scalar(str + pos, len - pos);
}

inline void lowercase_ascii_copy_avx2(const char *src, const int64_t len, char *dst)
{
  int64_t pos = 0;
  const __m256i upper_a = _mm256_set1_epi8('A' - 1);
  const __m256i upper_z = _mm256_set1_epi8('Z' + 1);
  const __m256i flip_mask = _mm256_set1_epi8(32);
  for (; pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + pos));
    const __m256i uppers = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, upper_a), _mm256_cmpgt_epi8(upper_z, bytes));
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + pos),
                        _mm256_xor_si256(bytes, _mm256_and_si256(uppers, flip_mask)));
  }
  oceanbase::storage::lowercase_ascii_copy_scalar(src + pos, len - pos, dst + pos);
}

inline bool is_ascii_document_avx2(const char *str, const int64_t len)
{
  bool is_ascii = true;
  int64_t pos = 0;
  for (; is_ascii && pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(str + pos));
    is_ascii = 0 == _mm256_movemask_epi8(bytes);
  }
  return is_ascii && oceanbase::storage::is_ascii_document_scalar(str + pos, len - pos);
}

)

inline int64_t count_leading_delimiter(const char *str, const int64_t len)
{
  int64_t skip = 0;
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && is_arch_supported(ObTargetArch::AVX2)) {
    skip = specific::avx2::count_leading_delimiter_avx2(str, len);
  } else
#endif
  {
    skip = count_leading_delimiter_scalar(str, len);
  }
  return skip;
}

inline int64_t count_leading_token(const char *str, const int64_t len)
{
  int64_t token_len = 0;
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && is_arch_supported(ObTargetArch::AVX2)) {
    token_len = specific::avx2::count_leading_token_avx2(str, len);
  } else
#endif
  {
    token_len = count_leading_token_scalar(str, len);
  }
  return token_len;
}

inline bool has_ascii_upper(const char *str, const int64_t len)
{
  bool found = false;
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && is_arch_supported(ObTargetArch::AVX2)) {
    found = specific::avx2::has_ascii_upper_avx2(str, len);
  } else
#endif
  {
    found = has_ascii_upper_scalar(str, len);
  }
  return found;
}

inline void lowercase_ascii_copy(const char *src, const int64_t len, char *dst)
{
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && is_arch_supported(ObTargetArch::AVX2)) {
    specific::avx2::lowercase_ascii_copy_avx2(src, len, dst);
  } else
#endif
  {
    lowercase_ascii_copy_scalar(src, len, dst);
  }
}

inline bool is_ascii_document_fast(const char *str, const int64_t len)
{
  bool is_ascii = true;
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && is_arch_supported(ObTargetArch::AVX2)) {
    is_ascii = specific::avx2::is_ascii_document_avx2(str, len);
  } else
#endif
  {
    is_ascii = is_ascii_document_scalar(str, len);
  }
  return is_ascii;
}
} // namespace

int ObBEngFTParser::get_next_token(
    const char *&word,
    int64_t &word_len,
    int64_t &char_len,
    int64_t &word_freq)
{
  int ret = OB_SUCCESS;
  ObDatum token;
  int64_t token_freq = 0;
  char *buf = nullptr;
  word = nullptr;
  word_len = 0;
  char_len = 0;
  word_freq = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("beng ft parser isn't initialized", K(ret), K(is_inited_));
  } else if (use_ascii_fast_path_) {
    ret = get_next_ascii_token(word, word_len, char_len, word_freq);
  } else if (OB_ISNULL(token_stream_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("token stream is nullptr", K(ret), KP(token_stream_));
  } else if (OB_FAIL(token_stream_->get_next(token, token_freq))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("fail to get next token", K(ret), KPC(token_stream_));
    }
  } else if (OB_ISNULL(token.ptr_) || OB_UNLIKELY(0 >= token.len_ || 0 >= token_freq)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(token.ptr_), K(token.len_), K(token_freq));
  } else if (OB_ISNULL(buf = static_cast<char *>(scratch_allocator_.alloc(token.len_)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate word memory", K(ret), K(token.len_));
  } else {
    MEMCPY(buf, token.ptr_, token.len_);
    word = buf;
    word_len = token.len_;
    char_len = token.len_;
    word_freq = token_freq;
    LOG_DEBUG("succeed to add word", K(ObString(word_len, word)), K(word_freq));
  }
  return ret;
}

bool ObBEngFTParser::is_ascii_document(const char *fulltext, const int64_t fulltext_len) const
{
  bool is_ascii = ObCharset::charset_type_by_coll(
                      static_cast<ObCollationType>(analysis_ctx_.cs_->number)) == CHARSET_UTF8MB4;
  if (is_ascii) {
    is_ascii = is_ascii_document_fast(fulltext, fulltext_len);
  }
  return is_ascii;
}

bool ObBEngFTParser::is_ascii_delimiter(const char ch) const
{
  return ob_isspace(analysis_ctx_.cs_, ch)
         || ob_iscntrl(analysis_ctx_.cs_, ch)
         || ob_ispunct(analysis_ctx_.cs_, ch);
}

int ObBEngFTParser::get_next_ascii_token(const char *&word,
                                         int64_t &word_len,
                                         int64_t &char_len,
                                         int64_t &word_freq)
{
  int ret = OB_SUCCESS;
  ascii_cur_ += count_leading_delimiter(ascii_cur_, ascii_end_ - ascii_cur_);
  if (ascii_cur_ >= ascii_end_) {
    ret = OB_ITER_END;
  } else {
    const char *start = ascii_cur_;
    const int64_t len = count_leading_token(start, ascii_end_ - start);
    const bool needs_casedown = has_ascii_upper(start, len);
    ascii_cur_ += len;
    if (!needs_casedown) {
      // The source document outlives word-map materialization. Returning its
      // slice directly avoids an allocation for already-lowercase ASCII words.
      word = start;
      word_len = len;
      char_len = len;
      word_freq = 1;
    } else {
      char *buf = static_cast<char *>(scratch_allocator_.alloc(len));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate ascii token", K(ret), K(len));
      } else {
        lowercase_ascii_copy(start, len, buf);
        word = buf;
        word_len = len;
        char_len = len;
        word_freq = 1;
      }
    }
  }
  return ret;
}

int ObBEngFTParser::init_analyzer()
{
  int ret = OB_SUCCESS;
  if (!analyzer_inited_) {
    if (OB_FAIL(english_analyzer_.init(analysis_ctx_, metadata_allocator_))) {
      LOG_WARN("failed to initialize english analyzer", K(ret), K_(analysis_ctx));
    } else {
      analyzer_inited_ = true;
    }
  }
  return ret;
}

int ObBEngFTParser::prepare_document(const char *fulltext, const int64_t fulltext_len)
{
  int ret = OB_SUCCESS;
  doc_.set_string(fulltext, fulltext_len);
  use_ascii_fast_path_ = is_ascii_document(fulltext, fulltext_len);
  if (use_ascii_fast_path_) {
    ascii_cur_ = fulltext;
    ascii_end_ = fulltext + fulltext_len;
    token_stream_ = nullptr;
  } else if (OB_FAIL(init_analyzer())) {
    LOG_WARN("failed to prepare english analyzer", K(ret));
  } else if (OB_FAIL(segment(doc_, token_stream_))) {
    LOG_WARN("failed to segment fulltext", K(ret));
  } else if (OB_ISNULL(token_stream_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("token stream is null", K(ret));
  }
  return ret;
}

int ObBEngFTParser::reuse_parser(const char *fulltext, const int64_t fulltext_len)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("basic english parser has not been initialized", K(ret));
  } else if (OB_ISNULL(fulltext) || fulltext_len <= 0 || UINT32_MAX < fulltext_len) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext for parser reuse", K(ret), KP(fulltext), K(fulltext_len));
  } else {
    scratch_allocator_.reset_remain_one_page();
    if (OB_FAIL(prepare_document(fulltext, fulltext_len))) {
      LOG_WARN("failed to reuse basic english parser", K(ret));
    }
  }
  return ret;
}

int ObBEngFTParser::init(ObFTParserParam *param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_ISNULL(param) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("param is nullptr", K(ret), KPC(param));
  } else if (OB_UNLIKELY(UINT32_MAX < param->ft_length_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("too large document, english analyzer hasn't be supported", K(ret), K(param->ft_length_));
  } else {
    analysis_ctx_.cs_ = param->cs_;
    analysis_ctx_.filter_stopword_ = false;
    analysis_ctx_.need_grouping_ = false;
    if (OB_FAIL(prepare_document(param->fulltext_, param->ft_length_))) {
      LOG_WARN("fail to prepare document by parser", K(ret), KP(param->fulltext_), K(param->ft_length_));
    } else {
      is_inited_ = true;
      LOG_DEBUG("succeed to init beng parser", K(ret), K(english_analyzer_), KPC(token_stream_), K(doc_));
    }
  }
  if (OB_FAIL(ret) && OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObBEngFTParser::segment(
    const common::ObDatum &doc,
    share::ObITokenStream *&token_stream)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(doc.ptr_) || OB_UNLIKELY(0 >= doc.len_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(doc.ptr_), K(doc.len_));
  } else if (OB_UNLIKELY(UINT32_MAX < doc.len_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("too large document, english analyzer hasn't be supported", K(ret), K(doc.len_));
  } else if (OB_FAIL(english_analyzer_.analyze(doc, token_stream))) {
    LOG_WARN("fail to analyze document", K(ret), K(english_analyzer_), KP(doc.ptr_), K(doc.len_));
  }
  return ret;
}

void ObBEngFTParser::reset()
{
  analysis_ctx_.reset();
  english_analyzer_.reset();
  scratch_allocator_.reset();
  doc_.reset();
  token_stream_ = nullptr;
  ascii_cur_ = nullptr;
  ascii_end_ = nullptr;
  use_ascii_fast_path_ = false;
  analyzer_inited_ = false;
  is_inited_ = false;
}

ObBasicEnglishFTParserDesc::ObBasicEnglishFTParserDesc()
  : is_inited_(false)
{
}

int ObBasicEnglishFTParserDesc::init(ObPluginParam *param)
{
  is_inited_ = true;
  return OB_SUCCESS;
}

int ObBasicEnglishFTParserDesc::deinit(ObPluginParam *param)
{
  reset();
  return OB_SUCCESS;
}

int ObBasicEnglishFTParserDesc::segment(
    ObFTParserParam *param,
    ObITokenIterator *&iter) const
{
  int ret = OB_SUCCESS;
  ObBEngFTParser *parser = nullptr;
  ObIAllocator *metadata_allocator = OB_NOT_NULL(param) && OB_NOT_NULL(param->metadata_allocator_)
                                      ? param->metadata_allocator_ : (OB_NOT_NULL(param) ? param->allocator_ : nullptr);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("default ft parser desc hasn't be initialized", K(ret), K(is_inited_));
  } else if (OB_ISNULL(param) || OB_ISNULL(param->fulltext_) || OB_UNLIKELY(!param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(param));
  } else if (OB_ISNULL(metadata_allocator)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parser metadata allocator is null", K(ret));
  } else if (OB_ISNULL(parser = OB_NEWx(ObBEngFTParser, metadata_allocator, *metadata_allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate basic english ft parser", K(ret));
  } else if (OB_FAIL(parser->init(param))) {
    LOG_WARN("fail to init basic english parser", K(ret), KPC(param));
  } else {
    iter = parser;
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(metadata_allocator)) {
    OB_DELETEx(ObBEngFTParser, metadata_allocator, parser);
  }

  return ret;
}

void ObBasicEnglishFTParserDesc::free_token_iter(
    ObFTParserParam *param,
    ObITokenIterator *&iter) const
{
  if (OB_NOT_NULL(iter)) {
    abort_unless(nullptr != param);
    ObIAllocator *metadata_allocator = OB_NOT_NULL(param->metadata_allocator_)
                                        ? param->metadata_allocator_ : param->allocator_;
    abort_unless(nullptr != metadata_allocator);
    iter->~ObITokenIterator();
    metadata_allocator->free(iter);
    iter = nullptr;
  }
}

int ObBasicEnglishFTParserDesc::get_add_word_flag(ObAddWordFlag &flag) const
{
  int ret = OB_SUCCESS;
  flag.set_min_max_word();
  flag.set_stop_word();
  flag.set_groupby_word();
  return ret;
}

} // end namespace storage
} // end namespace oceanbase
