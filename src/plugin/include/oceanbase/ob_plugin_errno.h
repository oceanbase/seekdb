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

/**
 * @addtogroup ObPlugin
 * @{
 */

/**
 * this is the adaptor errno of oceanbase errno
 */

#ifdef __cplusplus
extern "C" {
#endif

const int OBP_SUCCESS = 0;
const int OBP_INVALID_ARGUMENT = -4002;
const int OBP_INIT_TWICE = -4005;
const int OBP_NOT_INIT = -4006;
const int OBP_NOT_SUPPORTED = -4007;
const int OBP_ITER_END = -4008;
const int OBP_ALLOCATE_MEMORY_FAILED = -4013;
const int OBP_PLUGIN_ERROR = -11078;

#ifdef __cplusplus
} // extern "C"
#endif

/** @} */
