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
#ifndef TC_INFO
#define TC_INFO(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)
#endif

#ifndef ALLOCATE_QDTABLE
#define ALLOCATE_QDTABLE(size, name) static_cast<void**>(new (std::nothrow) void*[size])
#endif

#ifndef CACHE_ALIGNED
#define CACHE_ALIGNED __attribute__((aligned(64)))
#endif

#ifndef structof
#define structof(p, T, m) (T*)((char*)p - __builtin_offsetof(T, m))
#endif

#ifndef MAX_CPU_NUM
#define MAX_CPU_NUM 128
#endif

#define MAX_N_CHAN 16
#ifndef tc_itid
int next_itid_ = 0;
__thread int itid_ = -1;
int tc_itid() {
  if (itid_ >= 0) return itid_;
  itid_ = ATOMIC_FAA(&next_itid_, 1);
  return itid_;
}
#define tc_itid tc_itid
#endif
#define arrlen(x) (sizeof(x)/sizeof(x[0]))
