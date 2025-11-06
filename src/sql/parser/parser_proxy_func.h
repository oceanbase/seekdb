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

#ifndef PARSER_ALLOC_FUNC_H_
#define PARSER_ALLOC_FUNC_H_

#include <stdint.h>
// ObSQLParser module extracts a static link library for Proxy, Proxy must implement the following functions on its own to link correctly

void *parser_alloc_buffer(void *malloc_pool, const int64_t alloc_size);
void parser_free_buffer(void *malloc_pool, void *buffer);

#ifdef SQL_PARSER_COMPILATION
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

extern bool check_stack_overflow_c();
extern int check_stack_overflow_in_c(int *check_overflow);
extern void right_to_die_or_duty_to_live_c();

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // SQL_PARSER_COMPILATION

#endif // !PARSER_ALLOC_FUNC_H_
