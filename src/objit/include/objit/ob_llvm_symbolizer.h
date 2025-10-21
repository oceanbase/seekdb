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

#ifndef OCEANBASE_COMMON_LLVM_SYMBOLIZE_H_
#define OCEANBASE_COMMON_LLVM_SYMBOLIZE_H_
#ifndef ENABLE_SANITY
#else
#include <stdint.h>
#include <stddef.h>
#include <functional>
namespace oceanbase
{
namespace common
{
extern void print_symbol(void *addr, const char *func_name, const char *file_name, uint32_t line);
using SymbolizeCb = std::function<decltype(print_symbol)>;
extern int backtrace_symbolize(void **addrs, int32_t n_addr, SymbolizeCb cb);
extern int backtrace_symbolize(void **addrs, int32_t n_addr);
} // end namespace common
} // end namespace oceanbase

#endif //ENABLE_SANITY
#endif //OCEANBASE_COMMON_LLVM_SYMBOLIZE_H_
