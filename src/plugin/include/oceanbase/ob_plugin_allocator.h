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

#pragma once

#include "oceanbase/ob_plugin_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup ObPlugin
 * @{
 */

/**< The allocator paramter */
typedef ObPluginDatum ObPluginAllocatorPtr;

/**< malloc interface without label */
OBP_PUBLIC_API void *obp_malloc(int64_t size);

/**< malloc interface with alignment */
OBP_PUBLIC_API void *obp_malloc_align(int64_t alignment, int64_t size);

/**< free memory */
OBP_PUBLIC_API void obp_free(void *ptr);

/**
 * allocate memory with the specific allocator
 * @details you can get allocator from specific plugin interface, such as FTParser
 * @note A valid allocator always has a memory label
 */
OBP_PUBLIC_API void *obp_allocate(ObPluginAllocatorPtr allocator, int64_t size);

/**< allocate memory with alignment parameter */
OBP_PUBLIC_API void *obp_allocate_align(ObPluginAllocatorPtr allocator, int64_t alignment, int64_t size);

/**< free memory */
OBP_PUBLIC_API void obp_deallocate(ObPluginAllocatorPtr allocator, void *ptr);

/** @} */

#ifdef __cplusplus
} // extern "C"
#endif
