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
#ifndef OCEANBASE_TC_OB_TC_WRAPPER_H_
#define OCEANBASE_TC_OB_TC_WRAPPER_H_
#include "lib/oblog/ob_log.h"
#include "lib/ob_define.h"
#include "lib/thread_local/ob_tsi_utils.h"
#include "deps/oblib/src/lib/allocator/ob_malloc.h"
#define TC_INFO(...) _OB_LOG(INFO, __VA_ARGS__)
#define ALLOCATE_QDTABLE(size, name) static_cast<void**>(oceanbase::ob_malloc(size, name))
#define MAX_CPU_NUM oceanbase::common::OB_MAX_CPU_NUM
#define tc_itid oceanbase::common::get_itid
#include "ob_tc.cpp"
#endif /* OCEANBASE_TC_OB_TC_WRAPPER_H_ */
