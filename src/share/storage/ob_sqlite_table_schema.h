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

#ifndef OCEANBASE_SHARE_STORAGE_OB_SQLITE_TABLE_SCHEMA_H_
#define OCEANBASE_SHARE_STORAGE_OB_SQLITE_TABLE_SCHEMA_H_

// Unified header for SQLite table schema constants
// This header automatically includes the SQLite CREATE TABLE statements
// Usage: Simply include this header in your storage .cpp file

#define SQLITE_CREATE_TABLE_STATEMENTS
#include "share/inner_table/ob_inner_table_schema_misc.ipp"
#undef SQLITE_CREATE_TABLE_STATEMENTS

#endif // OCEANBASE_SHARE_STORAGE_OB_SQLITE_TABLE_SCHEMA_H_
