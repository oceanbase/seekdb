/**
 * Copyright (c) 2026 OceanBase.
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

#pragma once

#include <cstddef>
#include <stdint.h>
#include "lib/lock/ob_small_spin_lock.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace transaction
{

class ObTxCtx;

// A lifecycle-bound cache for constructed ObTxCtx objects. The cache owner can
// be closed before all borrowed objects are returned. Borrowed objects keep the
// small control block alive and are deleted instead of cached after close().
class TxCtxCache
{
public:
  static const int64_t SESSION_MAX_FREE_COUNT = 1;
  static const int64_t REPLAY_MAX_FREE_COUNT = 64;

  static TxCtxCache *create(int64_t max_free_count);
  static void close(TxCtxCache *&cache);
  static ObTxCtx *alloc(TxCtxCache *cache);
  static void free(ObTxCtx *ctx);

  int64_t get_hit_count() const;
  int64_t get_miss_count() const;

private:
  struct Node;

  explicit TxCtxCache(int64_t max_free_count);
  ~TxCtxCache();

  ObTxCtx *alloc();
  void free(Node *node, ObTxCtx *ctx);
  void close();
  void dec_ref();

  static Node *alloc_node(TxCtxCache *owner);
  static void free_node(Node *node);
  static ObTxCtx *node_to_ctx(Node *node);
  static Node *ctx_to_node(ObTxCtx *ctx);

private:
  common::ObByteLock lock_;
  int64_t ref_count_;
  Node *free_list_;
  int64_t free_count_;
  const int64_t max_free_count_;
  int64_t hit_count_;
  int64_t miss_count_;
  bool closed_;

  DISALLOW_COPY_AND_ASSIGN(TxCtxCache);
};

} // namespace transaction
} // namespace oceanbase
