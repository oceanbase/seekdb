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

#define private public
#undef private
#include "lib/allocator/ob_malloc.h"
#include "lib/coro/testing.h"
#include "lib/resource/ob_resource_mgr.h"
#include <gtest/gtest.h>

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace std;

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

class TestObjectMgr
    : public ::testing::Test
{
public:
  void *Malloc(uint64_t size)
  {
    // void *p = NULL;
    // AObject *obj = os_.alloc_object(size);
    // if (obj != NULL) {
    //   p = obj->data_;
    // }
    // return p;
    return oceanbase::common::ob_malloc(size, ObNewModIds::TEST);
  }

  void Free(void *ptr)
  {
    // AObject *obj = reinterpret_cast<AObject*>(
    //     (char*)ptr - AOBJECT_HEADER_SIZE);
    // os_.free_object(obj);
    return oceanbase::common::ob_free(ptr);
  }

// protected:
//   ObjectMgr<1> om_;
};

TEST_F(TestObjectMgr, Basic2)
{
  cotesting::FlexPool([] {
    void *p[128] = {};
    int64_t cnt = 1L << 18;
    uint64_t sz = 1L << 4;

    while (cnt--) {
      int i = 0;
      for (int j = 0; j < 16; ++j) {
        p[i++] = ob_malloc(sz, ObNewModIds::TEST);
      }
      while (i--) {
        ob_free(p[i]);
      }
      // sz = ((sz | reinterpret_cast<size_t>(p[0])) & ((1<<13) - 1));
    }

    cout << "done" << endl;
  }, 4).start();
}

TEST_F(TestObjectMgr, TestName)
{
  void *p[128];

  Malloc(327800);
  Malloc(65536);
  Malloc(49408);
  Malloc(12376);
  Malloc(65344);

  p[1] = Malloc(2097152);
  Free(p[1]);

  Malloc(49248);
  Malloc(3424);
  Malloc(8);
  Malloc(3072);
  Malloc(176);
  p[11] = Malloc(96);
  p[10] = Malloc(65536);
  p[7] = Malloc(65568);
  p[6] = Malloc(96);
  p[5] = Malloc(65536);
  Malloc(2096160);
  Malloc(16);
  Malloc(65536);
  p[9] = Malloc(96);
  p[8] = Malloc(65536);
  p[4] = Malloc(65568);
  p[3] = Malloc(96);
  p[2] = Malloc(65536);
  Malloc(16);
  Free(p[2]);
  Free(p[3]);
  Free(p[4]);
  Free(p[5]);
  Free(p[6]);
  Free(p[7]);
  Free(p[8]);
  Free(p[9]);
  Free(p[10]);
  Free(p[11]);
  Malloc(12384);
  Malloc(12384);
  Malloc(12384);
  Malloc(96);
  Malloc(65536);
  p[13] = Malloc(96);
  p[12] = Malloc(65536);
  Free(p[12]);
  Free(p[13]);
  p[15] = Malloc(96);
  p[14] = Malloc(65536);
  Free(p[14]);
  Free(p[15]);
  p[17] = Malloc(96);
  p[16] = Malloc(65536);
  Malloc(65536);
  Free(p[16]);
  Free(p[17]);
  p[19] = Malloc(96);
  p[18] = Malloc(65536);
  Free(p[18]);
  Free(p[19]);
  Malloc(96);
}

struct Record
{
  int32_t size_;
  int64_t addr_;
};

AChunk *chunk(void *ptr)
{
  auto *obj = (AObject*)((char*)ptr - AOBJECT_HEADER_SIZE);
  auto *chunk = obj->block()->chunk();
  return chunk;
}

TEST_F(TestObjectMgr, TestSubObjectMgr)
{
  AChunkMgr::instance().set_max_chunk_cache_size(0);
  oceanbase::lib::set_memory_limit(20LL<<30);
  int fd = open("alloc_flow_records", O_RDONLY, S_IRWXU | S_IRGRP);
  abort_unless(fd > 0);
  struct stat fileInfo;
  bzero(&fileInfo, sizeof(fileInfo));
  int rc = fstat(fd, &fileInfo);
  abort_unless(rc != -1);
  int64_t total_size = fileInfo.st_size;
  void *ptr = ::mmap(0, total_size, PROT_READ, MAP_SHARED, fd, 0);
  abort_unless(ptr != MAP_FAILED);
  int64_t ctx_id = ObCtxIds::DEFAULT_CTX_ID;
  auto ta = ObMallocAllocator::get_instance()->get_ctx_allocator(
    ctx_id);
  ObjectMgr som(*ta.ref_allocator(), false, INTACT_NORMAL_AOBJECT_SIZE, 1, false, NULL);
  ObMemAttr attr;
  ObResourceMgrHandle resource_handle;
  ObResourceMgr::get_instance().get_handle(
		  resource_handle);
  map<int64_t, AObject*> allocs;
  int i = total_size/sizeof(Record);
  auto *rec = (Record*)ptr;
  while (i--) {
    int32_t size = rec->size_;
    int64_t addr = rec->addr_;
    if (size != 0) {
      auto *object = som.alloc_object(size, attr);
      abort_unless(object != nullptr);
      allocs.insert(pair<int64_t, AObject*>(addr, object));
      memset(object->data_, 0xAA, size);
    } else {
      auto it = allocs.find(addr);
      abort_unless(it != allocs.end());
      AObject *obj = it->second;
      ABlock *block = obj->block();
      abort_unless(block->is_valid());
      ObjectSet *set = (ObjectSet *)block->obj_set_;
      set->free_object(obj);
      allocs.erase(it->first);
    }
    rec++;
  }
}
