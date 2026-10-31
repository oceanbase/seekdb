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
#include "storage/fts/ik/ob_ik_arbitrator.h"
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
  ObIKFTParser(ObIAllocator &allocator, ObFTDictHub *hub)
      : allocator_(allocator),
        is_inited_(false),
        coll_type_(ObCollationType::CS_TYPE_INVALID),
        ctx_(nullptr),
        hub_(hub),
        segmenters_(allocator_),
        dict_main_(nullptr),
        dict_quan_(nullptr),
        arb_()
  {
  }

  virtual ~ObIKFTParser() { reset(); }

  int init(const ObFTParserParam &param);

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

  int init_segmenter(const ObFTParserParam &param);

  int init_ctx(const ObFTParserParam &param);

  void reset();

  bool should_read_newest_table() const;

private:
  static constexpr int SEGMENT_LIMIT = 1000;
  ObIAllocator &allocator_;
  bool is_inited_;

  ObCollationType coll_type_;
  TokenizeContext *ctx_;
  ObFTDictHub *hub_;
  ObList<ObIIKProcessor *, ObIAllocator> segmenters_;

  // The dictionaries are built once and owned by the dict hub, so they are
  // shared by all the parsers and stay valid for the whole lifetime of the
  // process. For now there's no change of dict in one query, so we can pin
  // dict this level.
  const ObIFTDict *dict_main_;
  const ObIFTDict *dict_quan_;

  // Reused across batches of the same document; reset() (same cleanup as the
  // destructor) prepares it for the next batch.
  ObIKArbitrator arb_;

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
