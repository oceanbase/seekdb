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

#ifndef OCEANBASE_BLOCKSSTABLE_OB_CS_ENCODING_ALLOCATOR_H_
#define OCEANBASE_BLOCKSSTABLE_OB_CS_ENCODING_ALLOCATOR_H_

#include "lib/objectpool/ob_pool.h"
#include "lib/list/ob_dlink_node.h"
#include "lib/list/ob_dlist.h"
#include "ob_column_encoding_struct.h"
#include "ob_icolumn_cs_encoder.h"
#include "ob_icolumn_cs_decoder.h"
#include "ob_integer_column_encoder.h"
#include "ob_string_column_encoder.h"
#include "ob_int_dict_column_encoder.h"
#include "ob_str_dict_column_encoder.h"
#include "ob_integer_column_decoder.h"
#include "ob_string_column_decoder.h"
#include "ob_int_dict_column_decoder.h"
#include "ob_str_dict_column_decoder.h"
#include "semistruct_encoding/ob_semistruct_column_encoder.h"
#include "semistruct_encoding/ob_semistruct_column_decoder.h"

namespace oceanbase
{
namespace blocksstable
{

template <typename EncodingItem>
struct ObCSEncodingPool
{
  static const int64_t MAX_FREE_ITEM_CNT = 64;
  ObCSEncodingPool(const int64_t item_size, const ObMemAttr &attr);
  ~ObCSEncodingPool();

  template <typename T>
  inline int alloc(T *&item);
  inline void free(EncodingItem *item);

  EncodingItem* free_items_[MAX_FREE_ITEM_CNT];
  int64_t free_cnt_;
  common::ObPool<common::ObMalloc, common::ObNullLock> pool_;
};

extern int64_t cs_encoder_sizes[];
extern int64_t cs_decoder_sizes[];

template <typename EncodingItem>
class ObCSEncodingAllocator
{
public:
  typedef ObCSEncodingPool<EncodingItem> Pool;
  ObCSEncodingAllocator(const int64_t *size_array, const ObMemAttr &attr);
  virtual ~ObCSEncodingAllocator() {}
  int init();
  bool is_inited() const { return inited_; }
  template<typename T>
  inline int alloc(T *&item);
  inline void free(EncodingItem *item);
private:
  int add_pool(Pool *pool);
  bool inited_;
  int64_t size_index_;
  Pool integer_pool_;
  Pool string_pool_;
  Pool int_dict_pool_;
  Pool str_dict_pool_;
  Pool semistruct_pool_;
  Pool *pools_[ObCSColumnHeader::MAX_TYPE];
  int64_t pool_cnt_;
};

#include "ob_cs_encoding_allocator.ipp"

typedef ObCSEncodingAllocator<ObIColumnCSEncoder> ObCSEncoderAllocator;

}//end namespace blocksstable
}//end namespace oceanbase

#endif //OCEANBASE_ENCODING_OB_CS_ENCODING_ALLOCATOR_H_
