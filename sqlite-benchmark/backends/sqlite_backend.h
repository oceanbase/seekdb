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

/*
** backends/sqlite_backend.h — SQLite backend for speedtest1 benchmark.
**
** Thin wrapper: just includes the native sqlite3.h and defines the
** benchmark name macro.
*/

#ifndef SQLITE_BACKEND_H
#define SQLITE_BACKEND_H

#include "sqlite3.h"

#define SPEEDTEST_BACKEND_NAME "SQLite"

#endif /* SQLITE_BACKEND_H */
