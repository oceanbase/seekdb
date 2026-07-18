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
** db_adapter.h — Backend dispatch for speedtest1 benchmark.
**
** Select a backend at compile time:
**   -DUSE_BACKEND_SQLITE   use SQLite
**   -DUSE_BACKEND_SEEKDB   use SeekDB
*/

#if defined(USE_BACKEND_SQLITE)
  #include "backends/sqlite_backend.h"
#elif defined(USE_BACKEND_SEEKDB)
  #include "backends/seekdb_backend.h"
#else
  #error "No backend selected. Use -DUSE_BACKEND_SQLITE or -DUSE_BACKEND_SEEKDB"
#endif

/* DB_CLEANUP: remove the database directory/file before a fresh run */
#if defined(USE_BACKEND_SEEKDB)
  #define DB_CLEANUP(path) \
    do { char _cmd[512]; snprintf(_cmd, sizeof(_cmd), "rm -rf '%s'", (path)); system(_cmd); } while(0)
#elif defined(USE_BACKEND_SQLITE)
  #define DB_CLEANUP(path) remove(path)
#endif
