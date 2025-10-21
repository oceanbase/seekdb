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

#ifndef PARSER_UTILITY_H_
#define PARSER_UTILITY_H_

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int parser_to_hex_cstr(const void *in_data, const int64_t data_length, char *buff,
                const int64_t buff_size);


int parser_to_hex_cstr_(const void *in_data, const int64_t data_length,
                 char *buf, const int64_t buf_len,
                 int64_t *pos,
                 int64_t *cstr_pos);

#ifdef __cplusplus
}
#endif
#endif
