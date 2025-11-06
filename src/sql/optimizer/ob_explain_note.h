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

#ifndef _OB_SQL_OPTI_EXPLAIN_NOTE_DEF_H_
#define _OB_SQL_OPTI_EXPLAIN_NOTE_DEF_H_
namespace oceanbase
{
namespace sql
{

#define PDML_ENABLE_BY_TRACE_EVENT "PDML forcefully enabled because tracepoint event"
#define PDML_DISABLED_BY_JOINED_TABLES "PDML disabled because modify more than one target table in parallel is not supported yet"
#define PDML_DISABLED_BY_INSERT_UP "PDML disabled because it is an insert-on-duplicate-key-update query"
#define PDML_DISABLED_BY_TRIGGER "PDML disabled because the modified table has trigger"
#define PDML_DISABLED_BY_NESTED_SQL "PDML disabled because the modified table has foreign key/trigger/user defined function"
#define PDML_DISABLED_BY_LOCAL_UK "PDML disabled because the modified table has local unique index"
#define PDML_DISABLED_BY_GLOBAL_UK "PDML disabled because the modified table has global unique index in merge into statement"
#define PDML_DISABLE_BY_MERGE_UPDATE_PK "PDML disabled because the merge statement update primary key or unique index key"
#define PDML_DISABLED_BY_IGNORE "PDML disabled because it is an dml ignore query"
#define PDML_DISABLED_BY_UPDATE_NOW "PDML disabled by update now"
#define PARALLEL_ENABLED_BY_GLOBAL_HINT "Degree of Parallelism is %ld because of hint"
#define PARALLEL_ENABLED_BY_TABLE_HINT "Degree of Parallelism is %ld because of hint"
#define PARALLEL_ENABLED_BY_SESSION "Degree of Parallelism is %ld because of session"
#define PARALLEL_ENABLED_BY_TABLE_PROPERTY "Degree of Parallelisim is %ld because of table property"
#define PARALLEL_ENABLED_BY_AUTO_DOP "Degree of Parallelisim is %ld because of Auto DOP"
#define PARALLEL_DISABLED_BY_PL_UDF_DAS  "Degree of Parallelisim is %ld because stmt contain pl_udf which force das scan"
#define DIRECT_MODE_INSERT_INTO_SELECT  "Direct-mode %s is enabled in insert into select"
#define DIRECT_MODE_DISABLED_BY_PDML  "Direct-mode disabled because the pdml is disabled"
#define PARALLEL_DISABLED_BY_DBLINK  "Degree of Parallelisim is %ld because stmt contain dblink which force das scan"
#define PDML_DISABLED_BY_INSERT_PK_AUTO_INC "PDML disabled because the insert statement primary key or partition key has specified auto-increment column"
#define PDML_DISABLED_BY_TRANSFORMATIONS "PDML disabled because transformations like or-expansion"
#define INSERT_OVERWRITE_TABLE  "Overwrite table with full direct mode"
#define PARALLEL_DISABLED_BY_LICENSE "The degree of parallelism is set to %ld because current license does not allow this operation"
#define NON_STANDARD_COMPARISON_SETTING "Non-standard comparison level is set to %s because of %s"

}
}
#endif /* _OB_SQL_OPTI_EXPLAIN_NOTE_DEF_H_ */
//// end of header file

