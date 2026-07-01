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

#ifndef OCEANBASE_SHARE_OB_I_LOB_READ_SERVICE_
#define OCEANBASE_SHARE_OB_I_LOB_READ_SERVICE_

#include "lib/ob_define.h"
#include "common/object/ob_obj_type.h"

namespace oceanbase
{
namespace common
{
class ObString;
class ObIAllocator;
class ObLobLocatorV2;
struct ObLobTextIterCtx;
enum ObTextStringIterState : int;

// dependency-inversion port: complete abstract service surface for the lob-read domain。
// share layer(ObTextStringIter/ObDeltaLob/ObLobTextIterCtx)reads out-of-row lob through this port,
// no longer depends directly on storage::ObLobManager / ObLobAccessParam and other storage types。
// storage layer(ObLobManager)implements this port, injected through MTL(ObILobReadService*)。
//
// constraint: all inputs/outputs are share-level types(ObLobTextIterCtx&/ObString/ObIAllocator*/
//       ObObjType/ObCollationType/ObTextStringIterState&), no storage types appear;
//       ObLobAccessParam construction and query/getlength calls stay locked inside the storage implementation。
class ObILobReadService
{
public:
  virtual ~ObILobReadService() {}

  // read the full out-of-row lob data, writes the result to ctx.buff_ / ctx.content_byte_len_。
  virtual int get_outrow_lob_full_data(ObLobTextIterCtx &ctx,
                                       ObCollationType cs_type,
                                       bool has_lob_header,
                                       bool is_outrow,
                                       ObIAllocator *tmp_alloc) = 0;

  // read the full json-delta out-of-row lob data(including partial-data merging), writes the result to ctx and data_str。
  virtual int get_delta_lob_full_data(ObLobTextIterCtx &ctx,
                                      ObObjType type,
                                      ObCollationType cs_type,
                                      ObLobLocatorV2 &lob_locator,
                                      ObIAllocator *allocator,
                                      ObString &data_str) = 0;

  // read prefix data from the out-of-row lob(prefix_char_len characters), writes the result to ctx.buff_ / ctx.content_byte_len_。
  virtual int get_outrow_prefix_data(ObLobTextIterCtx &ctx,
                                     ObCollationType cs_type,
                                     bool has_lob_header,
                                     bool is_outrow,
                                     ObIAllocator *tmp_alloc,
                                     uint32_t prefix_char_len) = 0;

  // start the out-of-row lob query iter and fetch the first chunk, writes the result to ctx and str, advances state。
  virtual int get_first_block(ObLobTextIterCtx &ctx,
                              ObCollationType cs_type,
                              bool has_lob_header,
                              bool is_outrow,
                              ObIAllocator *tmp_alloc,
                              ObString &str,
                              ObTextStringIterState &state) = 0;

  // fetch the next out-of-row lob chunk(query iter is already ready, reserve has already been done on the share side), writes the result to ctx and str, advances state。
  virtual int get_next_block_inner(ObLobTextIterCtx &ctx,
                                   ObCollationType cs_type,
                                   bool has_lob_header,
                                   bool is_outrow,
                                   ObString &str,
                                   ObTextStringIterState &state) = 0;

  // get the character length of the out-of-row lob。
  virtual int get_outrow_char_len(ObLobTextIterCtx &ctx,
                                  ObCollationType cs_type,
                                  ObIAllocator *tmp_alloc,
                                  int64_t &char_length) = 0;

  // release the storage query iter held by ctx(cleaned during destruction/reuse, share side does not hold a storage complete type)。
  virtual void free_lob_query_iter(ObLobTextIterCtx &ctx) = 0;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_I_LOB_READ_SERVICE_
