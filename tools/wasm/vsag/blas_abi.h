// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#define F77_GLOBAL(lower, upper) lower##_
#define F77_GLOBAL_(lower, upper) lower##_
#ifdef __cplusplus
static_assert(sizeof(long) == sizeof(int32_t), "f2c integer must match LAPACKE integer");
#else
_Static_assert(sizeof(long) == sizeof(int32_t), "f2c integer must match LAPACKE integer");
#endif
