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
#include "storage/fts/dict/ob_ft_cache_container.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/ik/ob_ik_processor.h"
#include "data_plane/fts/ob_fts_parser.h"

#include <cstdint>
namespace oceanbase
{
namespace storage
{
class ObFTDictHub;

class ObIKFTParser final : public ObITokenIterator
{
public:
  // Single-allocator form (per-call usage): object and per-call scratch share
  // one allocator (historically the eval arena passed via ObFTParserParam).
  ObIKFTParser(ObIAllocator &allocator, ObFTDictHub *hub)
      : ObIKFTParser(allocator, allocator, hub)
  {
  }

  // Two-allocator form (pooled reuse): obj_alloc_ owns everything that lives
  // for the whole pool-slot lifetime (context object, segmenters, pinned
  // dicts and their dict-hub containers); allocator_ is the per-call scratch
  // arena, recycled by release_to_pool() when the parser goes back to sleep
  // in its slot.
  ObIKFTParser(ObIAllocator &scratch_alloc, ObIAllocator &obj_alloc, ObFTDictHub *hub)
      : allocator_(scratch_alloc),
        obj_alloc_(obj_alloc),
        is_inited_(false),
        pooled_(false),
        built_(false),
        coll_type_(ObCollationType::CS_TYPE_INVALID),
        ctx_(nullptr),
        hub_(hub),
        segmenters_{nullptr, nullptr, nullptr, nullptr},
        cache_main_(obj_alloc),
        cache_quan_(obj_alloc),
        cache_stop_(obj_alloc),
        dict_main_(nullptr),
        dict_quan_(nullptr),
        dict_stop_(nullptr)
  {
  }

  virtual ~ObIKFTParser() { reset(); }

  int init(const ObFTParserParam &param);

  // Re-apply the per-fulltext state on top of the persistent objects built by
  // init(): collation, fulltext pointer/length, smart mode and every
  // cross-call mutable state enumerated in release_to_pool(). The dicts
  // pinned by init() are reused as-is: their content is a pure function of
  // the compiled-in dict text and the (type, range_id) cache keys, so it
  // cannot drift between two calls.
  int reuse_parser(const ObFTParserParam &param);

  // Drop every piece of per-call scratch state so the parser can sleep in its
  // pool slot without referencing recycled memory or stale text. Walks the
  // per-call containers while their memory is valid; the caller recycles the
  // scratch allocator afterwards. No-op for a non-pooled parser (its scratch
  // is the eval arena and free_token_iter destroys the object as before).
  void release_to_pool();

  bool is_built() const { return built_; }
  bool is_inited() const { return is_inited_; }
  bool is_pooled() const { return pooled_; }
  void set_pooled() { pooled_ = true; }

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
  int init_dict(const ObFTParserParam &param);

  int init_single_dict(ObFTDictDesc desc, ObFTCacheRangeContainer &container);

  int init_segmenter(const ObFTParserParam &param);

  int init_ctx(const ObFTParserParam &param);

  void reset();

  bool should_read_newest_table() const;

  int build_dict_from_cache(const ObFTDictDesc &desc,
                            ObFTCacheRangeContainer &container,
                            ObIFTDict *&dict);

private:
  static constexpr int SEGMENT_LIMIT = 1000;
  // per-call scratch (dicts were per-call in the legacy layout, ctx internals
  // always are); for the pooled layout this is the slot scratch arena
  ObIAllocator &allocator_;
  // long-lived object storage: ctx object, segmenters, pinned dicts; equals
  // allocator_ in the legacy single-allocator layout
  ObIAllocator &obj_alloc_;
  bool is_inited_;
  bool pooled_;
  // persistent objects (ctx object, segmenters, pinned dicts) are built and
  // usable; cleared only by reset(), never by release_to_pool()
  bool built_;

  ObCollationType coll_type_;
  TokenizeContext *ctx_;
  ObFTDictHub *hub_;
  // fixed slot array instead of an ObList: 4 segmenters, insertion order
  // (letter -> quantifier -> cjk -> surrogate) is the processing order and
  // is preserved; the per-character loop no longer walks list nodes.
  static constexpr int64_t SEGMENTER_SLOT_CNT = 4;
  ObIIKProcessor *segmenters_[SEGMENTER_SLOT_CNT];

  // For now there's no change of dict in one query, so we can pin dict this level.
  ObFTCacheRangeContainer cache_main_;
  ObFTCacheRangeContainer cache_quan_;
  ObFTCacheRangeContainer cache_stop_;

  ObIFTDict *dict_main_;
  ObIFTDict *dict_quan_;
  ObIFTDict *dict_stop_;

  DISABLE_COPY_ASSIGN(ObIKFTParser);
};

class ObIKFTParserDesc final : public ObIFTParserDesc
{
public:
  ObIKFTParserDesc() {}
  virtual ~ObIKFTParserDesc() = default;
  virtual int segment(ObFTParserParam *param, ObITokenIterator *&iter) const override;
  virtual void free_token_iter(ObFTParserParam *param, ObITokenIterator *&iter) const override;
  virtual int get_add_word_flag(ObAddWordFlag &flag) const override;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_OB_IK_FT_PARSER_H_
