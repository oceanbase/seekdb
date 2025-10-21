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

#ifndef _OCEANBASE_STORAGE_FTS_DICT_OB_FT_CACHE_DICT_H_
#define _OCEANBASE_STORAGE_FTS_DICT_OB_FT_CACHE_DICT_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/cache/ob_kv_storecache.h"
#include "storage/fts/dict/ob_ft_cache.h"
#include "storage/fts/dict/ob_ft_dat_dict.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"

namespace oceanbase
{
namespace storage
{
class ObFTCacheDict final : public ObIFTDict
{
public:
  ObFTCacheDict(ObCollationType coll_type, ObFTDAT *dat)
      : coll_type_(coll_type), dat_(dat), reader_(dat)
  {
  }
  int init() override;
  int match(const ObString &single_word, ObDATrieHit &hit) const override;
  int match(const ObString &words, bool &is_match) const override;
  int match_with_hit(const ObString &single_word,
                     const ObDATrieHit &last_hit,
                     ObDATrieHit &hit) const override;

public:
  static int make_and_fetch_cache_entry(const ObFTDictDesc &desc,
                                        ObFTDAT *dat_buff,
                                        const size_t buff_size,
                                        const int32_t range_id,
                                        const ObDictCacheValue *&value,
                                        ObKVCacheHandle &handle);

private:
  ObKVCacheHandle handle_; // used to pin the mem block later
  ObCollationType coll_type_;
  ObFTDAT *dat_ = nullptr;
  ObFTDATReader<void> reader_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObFTCacheDict);
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_CACHE_DICT_H_
