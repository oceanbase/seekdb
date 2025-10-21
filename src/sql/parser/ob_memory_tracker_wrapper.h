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

#ifndef OCEANBASE_SQL_OB_MEMORY_TRACKER_WRAPPER_H
#define OCEANBASE_SQL_OB_MEMORY_TRACKER_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int check_mem_status();
int try_check_mem_status(int64_t check_try_times);

#ifdef __cplusplus
}
#endif

#endif /* OCEANBASE_SQL_OB_MEMORY_TRACKER_WRAPPER_H */
