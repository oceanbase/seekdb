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
 
#ifndef SRC_COMMON_META_PROGRAMMING_OB_META_DEFINE_H
#define SRC_COMMON_META_PROGRAMMING_OB_META_DEFINE_H
#include "lib/allocator/ob_allocator.h"
namespace oceanbase
{
namespace common
{
namespace meta
{

struct DummyAllocator : public ObIAllocator
{
virtual void *alloc(const int64_t) override { return nullptr; }
virtual void* alloc(const int64_t, const ObMemAttr &) { return nullptr; }
virtual void free(void *ptr) override {}
static DummyAllocator &get_instance() { static DummyAllocator alloc; return alloc; }
};

}
}
}
#endif
