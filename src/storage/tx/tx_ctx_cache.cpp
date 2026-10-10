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

#include <cstddef>
#include <new>
#include "data_plane/ob_tx_ctx_cache_api.h"
#include "lib/atomic/ob_atomic.h"
#include "storage/tx/ob_tx_ctx.h"
#include "storage/tx/tx_ctx_cache.h"

namespace oceanbase
{
namespace transaction
{

struct TxCtxCache::Node
{
  TxCtxCache *owner_;
  Node *next_;
  void *raw_;
};

namespace
{
constexpr size_t TX_CTX_ALIGNMENT = alignof(ObTxCtx);
static_assert(0 == (TX_CTX_ALIGNMENT & (TX_CTX_ALIGNMENT - 1)),
              "ObTxCtx alignment must be a power of two");
}

TxCtxCache::TxCtxCache(const int64_t max_free_count)
    : lock_(),
      ref_count_(1),
      free_list_(nullptr),
      free_count_(0),
      max_free_count_(max_free_count),
      hit_count_(0),
      miss_count_(0),
      closed_(false)
{
}

TxCtxCache::~TxCtxCache()
{
  OB_ASSERT(closed_);
  OB_ASSERT(nullptr == free_list_);
  OB_ASSERT(0 == free_count_);
  OB_ASSERT(0 == ATOMIC_LOAD(&ref_count_));
}

TxCtxCache *TxCtxCache::create(const int64_t max_free_count)
{
  return max_free_count > 0
      ? new (std::nothrow) TxCtxCache(max_free_count)
      : nullptr;
}

void TxCtxCache::close(TxCtxCache *&cache)
{
  TxCtxCache *tmp = cache;
  cache = nullptr;
  if (nullptr != tmp) {
    tmp->close();
  }
}

ObTxCtx *TxCtxCache::alloc(TxCtxCache *cache)
{
  ObTxCtx *ctx = nullptr;
  if (nullptr != cache) {
    ctx = cache->alloc();
  } else {
    Node *node = alloc_node(nullptr);
    ctx = nullptr == node ? nullptr : node_to_ctx(node);
  }
  return ctx;
}

void TxCtxCache::free(ObTxCtx *ctx)
{
  if (nullptr != ctx) {
    Node *node = ctx_to_node(ctx);
    if (nullptr == node->owner_) {
      free_node(node);
    } else {
      node->owner_->free(node, ctx);
    }
  }
}

ObTxCtx *TxCtxCache::alloc()
{
  Node *node = nullptr;
  bool accepted = false;
  {
    common::ObByteLockGuard guard(lock_);
    if (!closed_) {
      accepted = true;
      ATOMIC_INC(&ref_count_);
      if (nullptr != free_list_) {
        node = free_list_;
        free_list_ = node->next_;
        node->next_ = nullptr;
        --free_count_;
        ATOMIC_INC(&hit_count_);
      } else {
        ATOMIC_INC(&miss_count_);
      }
    }
  }

  if (accepted && nullptr == node) {
    node = alloc_node(this);
    if (nullptr == node) {
      dec_ref();
    }
  } else if (!accepted) {
    node = alloc_node(nullptr);
  }
  return nullptr == node ? nullptr : node_to_ctx(node);
}

void TxCtxCache::free(Node *node, ObTxCtx *ctx)
{
  bool cached = false;
  ctx->reset();
  {
    common::ObByteLockGuard guard(lock_);
    if (!closed_ && free_count_ < max_free_count_) {
      node->next_ = free_list_;
      free_list_ = node;
      ++free_count_;
      cached = true;
    }
  }
  if (!cached) {
    free_node(node);
  }
  dec_ref();
}

void TxCtxCache::close()
{
  Node *list = nullptr;
  bool need_dec_ref = false;
  {
    common::ObByteLockGuard guard(lock_);
    if (!closed_) {
      closed_ = true;
      list = free_list_;
      free_list_ = nullptr;
      free_count_ = 0;
      need_dec_ref = true;
    }
  }
  while (nullptr != list) {
    Node *next = list->next_;
    free_node(list);
    list = next;
  }
  if (need_dec_ref) {
    dec_ref();
  }
}

void TxCtxCache::dec_ref()
{
  if (0 == ATOMIC_SAF(&ref_count_, 1)) {
    delete this;
  }
}

TxCtxCache::Node *TxCtxCache::alloc_node(TxCtxCache *owner)
{
  const size_t alloc_size = sizeof(Node) + TX_CTX_ALIGNMENT - 1 + sizeof(ObTxCtx);
  void *raw = ::operator new(alloc_size, std::nothrow);
  Node *node = nullptr;
  if (nullptr != raw) {
    const uintptr_t ctx_addr =
        (reinterpret_cast<uintptr_t>(raw) + sizeof(Node) + TX_CTX_ALIGNMENT - 1)
        & ~(TX_CTX_ALIGNMENT - 1);
    node = reinterpret_cast<Node *>(ctx_addr - sizeof(Node));
    node->owner_ = owner;
    node->next_ = nullptr;
    node->raw_ = raw;
    new (node_to_ctx(node)) ObTxCtx();
  }
  return node;
}

void TxCtxCache::free_node(Node *node)
{
  if (nullptr != node) {
    void *raw = node->raw_;
    node_to_ctx(node)->~ObTxCtx();
    ::operator delete(raw);
  }
}

ObTxCtx *TxCtxCache::node_to_ctx(Node *node)
{
  return reinterpret_cast<ObTxCtx *>(reinterpret_cast<char *>(node) + sizeof(Node));
}

TxCtxCache::Node *TxCtxCache::ctx_to_node(ObTxCtx *ctx)
{
  return reinterpret_cast<Node *>(reinterpret_cast<char *>(ctx) - sizeof(Node));
}

int64_t TxCtxCache::get_hit_count() const
{
  return ATOMIC_LOAD(&hit_count_);
}

int64_t TxCtxCache::get_miss_count() const
{
  return ATOMIC_LOAD(&miss_count_);
}

} // namespace transaction

namespace data_plane
{

void *create_tx_ctx_cache(const int64_t max_free_count)
{
  return transaction::TxCtxCache::create(max_free_count);
}

void close_tx_ctx_cache(void *&cache)
{
  transaction::TxCtxCache *typed_cache =
      static_cast<transaction::TxCtxCache *>(cache);
  transaction::TxCtxCache::close(typed_cache);
  cache = nullptr;
}

} // namespace data_plane
} // namespace oceanbase
