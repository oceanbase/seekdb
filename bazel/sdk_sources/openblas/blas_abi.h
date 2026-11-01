/**
 * Copyright (c) 2026 OceanBase.
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
#include <stdint.h>
#define F77_GLOBAL(lower, upper) lower##_
#define F77_GLOBAL_(lower, upper) lower##_
#ifdef __cplusplus
static_assert(sizeof(long) == sizeof(int32_t), "f2c integer must match LAPACKE integer");
#else
_Static_assert(sizeof(long) == sizeof(int32_t), "f2c integer must match LAPACKE integer");
#endif
