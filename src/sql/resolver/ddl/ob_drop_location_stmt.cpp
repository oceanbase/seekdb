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

 #include "sql/resolver/ddl/ob_drop_location_stmt.h"

 using namespace oceanbase::common;
 using namespace oceanbase::share::schema;

 namespace oceanbase
 {
 namespace sql
 {
 ObDropLocationStmt::ObDropLocationStmt()
   : ObDDLStmt(stmt::T_DROP_LOCATION),
     arg_()
 {
 }

 ObDropLocationStmt::ObDropLocationStmt(common::ObIAllocator *name_pool)
   : ObDDLStmt(name_pool, stmt::T_DROP_LOCATION),
     arg_()
 {
 }

 ObDropLocationStmt::~ObDropLocationStmt()
 {
 }
 } // namespace sql
 } // namespace oceanbase
