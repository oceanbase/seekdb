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

#ifndef OBLIB_CO_VAR_H
#define OBLIB_CO_VAR_H

#include <stdint.h>
#include <cstdlib>

// RLOCAL_EXTERN -- declaration of extern CoVar in header file
// RLOCAL_STATIC -- declaration of static class member CoVar
// _RLOCAL       -- defination of static and extern CoVar
// RLOCAL        -- defination of local CoVar
// RLOCAL_INLINE -- defination of local CoVar in INLINE/template/static function

// TYPE must not be array, pls use Wrapper.

template<int N> using ByteBuf = char[N];

#define RLOCAL_EXTERN(TYPE, VAR) extern thread_local TYPE VAR
#define RLOCAL_STATIC(TYPE, VAR) static thread_local TYPE VAR
#define _RLOCAL(TYPE, VAR) thread_local TYPE VAR
#define RLOCAL(TYPE, VAR) thread_local TYPE VAR
#define RLOCAL_INLINE(TYPE, VAR) thread_local TYPE VAR
#define RLOCAL_INIT(TYPE, VAR, INIT) thread_local TYPE VAR = INIT

#endif /* OBLIB_CO_VAR_H */
