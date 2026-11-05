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

#include "ob_slice_alloc.h"


namespace oceanbase {
namespace common {

void ObBlockSlicer::print_leak_slice() {
  if (OB_ISNULL(slice_alloc_)) {
    LIB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "invalid slice allocator", KP(this));
    return;
  }

  int32_t limit = slice_alloc_->get_bsize();
  int32_t slice_size = slice_alloc_->get_isize();
  int64_t isize = lib::align_up2((int32_t)sizeof(Item) + slice_size, 16);
  int64_t total = (limit - (int32_t)sizeof(*this)) / (isize + (int32_t)sizeof(void *));
  char *istart = (char *)lib::align_up2((uint64_t)((char *)(this + 1) + sizeof(void *) * total), 16);
  if (istart + isize * total > ((char *)this) + limit) {
    total--;
  }

  LIB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "", K(limit), K(slice_size), K(isize), K(total), KP(istart));
  
  for (int32_t i = 0; i < total; i++) {
    Item *item = (Item *)(istart + i * isize);
    void *slice = (void *)(item + 1);
    LIB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "", KP(item), KP(slice));
    if (flist_.is_in_queue(item)) {
      // this item has been freed
    } else {
      LIB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "leak info : ", KP(item), KP(slice));
    }
  }
}

void ObSliceAlloc::destroy()
{
  for (int i = MAX_ARENA_NUM - 1; i >= 0; i--) {
    Arena &arena = arena_[i];
    Block *old_blk = arena.clear();
    if (NULL != old_blk) {
      blk_ref_[ObBlockSlicer::hash((uint64_t)old_blk) % MAX_REF_NUM].sync();
      if (old_blk->release()) {
        blk_list_.add(&old_blk->dlink_);
      }
    }
  }
  ObDLink *dlink = nullptr;
  dlink = blk_list_.top();
  while (OB_NOT_NULL(dlink)) {
    Block *blk = CONTAINER_OF(dlink, Block, dlink_);
    if (blk->recycle()) {
      destroy_block(blk);
      dlink = blk_list_.top();
    } else {
      blk->print_leak_slice();
      if (attr_.label_.is_valid() && 0 == strncmp("Diagnostic", attr_.label_.str_, 10)) {
        _LIB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED,
            "there was memory leak, label=%s, stock=%d, total=%d, remain=%d", attr_.label_.str_,
            blk->stock(), blk->total(), blk->remain());
      } else {
        _LIB_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED,
            "there was memory leak, stock=%d, total=%d, remain=%d", blk->stock(), blk->total(),
            blk->remain());
      }
      dlink = nullptr;  // break
    }
  }
  tmallocator_ = NULL;
  bsize_ = 0;
}

} // namespace common
} // namespace oceanbase
