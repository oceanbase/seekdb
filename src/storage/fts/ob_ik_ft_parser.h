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

#ifndef _OCEANBASE_STORAGE_FTS_OB_IK_FT_PARSER_H_
#define _OCEANBASE_STORAGE_FTS_OB_IK_FT_PARSER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "storage/fts/dict/ob_ft_cache_container.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/ik/ob_ik_arbitrator.h"
#include "storage/fts/ik/ob_ik_processor.h"
#include "plugin/interface/ob_plugin_ftparser_intf.h"

#include <cstdint>
namespace oceanbase
{
namespace storage
{
class ObFTDictHub;
class ObIKLetterProcessor;
class ObIKQuantifierProcessor;
class ObIKCJKProcessor;
class ObIKSurrogateProcessor;

class ObIKFTParser final : public plugin::ObITokenIterator
{
private:
  struct CachedToken
  {
    int64_t relative_offset_;
    int64_t length_;
    int64_t char_cnt_;
    ObIKTokenType type_;
    int64_t word_freq_;
    TO_STRING_KV(K_(relative_offset), K_(length), K_(char_cnt), K_(type), K_(word_freq));
  };

  struct CachedSegment
  {
    CachedSegment() : hash_(0), coll_type_(CS_TYPE_INVALID), is_smart_(false), text_(), tokens_() {}
    uint64_t hash_;
    ObCollationType coll_type_;
    bool is_smart_;
    ObString text_;
    common::ObSEArray<CachedToken, 16> tokens_;
    TO_STRING_KV(K_(hash), K_(coll_type), K_(is_smart), K_(text), K_(tokens));
  };

public:
  ObIKFTParser(ObIAllocator &allocator, ObFTDictHub *hub)
      : allocator_(allocator),
        scratch_allocator_(lib::ObMemAttr("IKParserScratch")),
        segment_cache_allocator_(lib::ObMemAttr("IKSegmentCache")),
        is_inited_(false),
        coll_type_(ObCollationType::CS_TYPE_INVALID),
        ctx_(nullptr),
        hub_(hub),
        arbitrator_(),
        segmenters_(allocator_),
        letter_processor_(nullptr),
        quantifier_processor_(nullptr),
        cjk_processor_(nullptr),
        surrogate_processor_(nullptr),
        cache_main_(allocator),
        cache_quan_(allocator),
        cache_stop_(allocator),
        dict_main_(nullptr),
        dict_quan_(nullptr),
        dict_stop_(nullptr),
        document_fulltext_(nullptr),
        document_fulltext_len_(0),
        next_segment_offset_(0),
        current_segment_offset_(0),
        current_segment_len_(0),
        document_is_smart_(false),
        use_segment_cache_(false),
        current_segment_cache_hit_(false),
        current_cached_segment_(nullptr),
        current_cached_token_idx_(0),
        segment_cache_bytes_(0),
        segment_cache_()
  {
  }

  virtual ~ObIKFTParser() { reset(); }

  int init(const plugin::ObFTParserParam &param);

  int start_document(const plugin::ObFTParserParam &param);

  int get_next_token(const char *&word,
                     int64_t &word_len,
                     int64_t &char_cnt,
                     int64_t &word_freq) override;

  VIRTUAL_TO_STRING_KV(K(is_inited_));

private:
  int produce();

  int process_next_batch();

  int process_one_char(TokenizeContext &ctx,
                       const char *ch,
                       const uint8_t char_len,
                       const ObFTCharUtil::CharType type);

private:
  int init_dict(const plugin::ObFTParserParam &param);

  int init_single_dict(ObFTDictDesc desc, ObFTCacheRangeContainer &container);

  int init_segmenter(const plugin::ObFTParserParam &param);

  int init_ctx(const plugin::ObFTParserParam &param);

  int init_next_segment();

  int find_next_segment(int64_t &segment_len) const;

  int lookup_segment_cache(const char *text,
                           const int64_t text_len,
                           CachedSegment *&entry) const;

  int save_current_segment();

  void clear_segment_cache();

  void reset_segment_state();

  void reset_document_state();

  void reset();

  bool should_read_newest_table() const;

  int build_dict_from_cache(const ObFTDictDesc &desc,
                            ObFTCacheRangeContainer &container,
                            ObIFTDict *&dict);

private:
  static constexpr int SEGMENT_LIMIT = 1000;
  ObIAllocator &allocator_;
  common::ObArenaAllocator scratch_allocator_;
  common::ObArenaAllocator segment_cache_allocator_;
  bool is_inited_;

  ObCollationType coll_type_;
  TokenizeContext *ctx_;
  ObFTDictHub *hub_;
  ObIKArbitrator arbitrator_;
  ObList<ObIIKProcessor *, ObIAllocator> segmenters_;
  ObIKLetterProcessor *letter_processor_;
  ObIKQuantifierProcessor *quantifier_processor_;
  ObIKCJKProcessor *cjk_processor_;
  ObIKSurrogateProcessor *surrogate_processor_;

  // For now there's no change of dict in one query, so we can pin dict this level.
  ObFTCacheRangeContainer cache_main_;
  ObFTCacheRangeContainer cache_quan_;
  ObFTCacheRangeContainer cache_stop_;

  ObIFTDict *dict_main_;
  ObIFTDict *dict_quan_;
  ObIFTDict *dict_stop_;

  const char *document_fulltext_;
  int64_t document_fulltext_len_;
  int64_t next_segment_offset_;
  int64_t current_segment_offset_;
  int64_t current_segment_len_;
  bool document_is_smart_;
  bool use_segment_cache_;
  bool current_segment_cache_hit_;
  CachedSegment *current_cached_segment_;
  int64_t current_cached_token_idx_;
  int64_t segment_cache_bytes_;
  common::ObSEArray<CachedSegment *, 256> segment_cache_;

  static constexpr int64_t MAX_SEGMENT_CACHE_ENTRIES = 256;
  static constexpr int64_t MAX_SEGMENT_CACHE_BYTES = 4L * 1024L * 1024L;

  DISABLE_COPY_ASSIGN(ObIKFTParser);
};

class ObIKFTParserDesc final : public plugin::ObIFTParserDesc
{
public:
  ObIKFTParserDesc() {}
  virtual ~ObIKFTParserDesc() = default;
  virtual int init(plugin::ObPluginParam *param) override;
  virtual int deinit(plugin::ObPluginParam *param) override;
  virtual int segment(plugin::ObFTParserParam *param, plugin::ObITokenIterator *&iter) const override;
  virtual void free_token_iter(plugin::ObFTParserParam *param,
                               plugin::ObITokenIterator *&iter) const override;
  virtual int get_add_word_flag(ObAddWordFlag &flag) const override;
  OB_INLINE void reset() { is_inited_ = false; }

private:
  bool is_inited_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_OB_IK_FT_PARSER_H_
