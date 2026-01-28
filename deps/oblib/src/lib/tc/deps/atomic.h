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
#ifndef ATOMIC_LOAD
#define ATOMIC_LOAD(x) __atomic_load_n((x), __ATOMIC_ACQUIRE)
#define ATOMIC_STORE(x, v) __atomic_store_n((x), (v), __ATOMIC_RELEASE)
#define ATOMIC_FAA(val, addv) __sync_fetch_and_add((val), (addv))
#define ATOMIC_AAF(val, addv) __sync_add_and_fetch((val), (addv))
#define ATOMIC_TAS(val, newv) __sync_lock_test_and_set((val), (newv))
#define ATOMIC_VCAS(val, cmpv, newv) __sync_val_compare_and_swap((val), (cmpv), (newv))
#define ATOMIC_BCAS(val, cmpv, newv) __sync_bool_compare_and_swap((val), (cmpv), (newv))
#if defined(__x86_64__) || defined(__i386__)
#define PAUSE() __asm__ volatile("pause\n" ::: "memory")
#elif defined(__aarch64__) || defined(__arm__)
#define PAUSE() __asm__ volatile("yield\n" ::: "memory")
#else
#define PAUSE() ((void)0)
#endif
#endif
