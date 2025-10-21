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

FUNCTION_TYPE(MVIEW_JOB) //materialized view
FUNCTION_TYPE(STAT_MAINTENANCE_JOB, SHADOW) //Statistics information
FUNCTION_TYPE(OLAP_ASYNC_JOB) //olap asynchronous job
FUNCTION_TYPE(MYSQL_EVENT_JOB) //mysql_event
FUNCTION_TYPE(NODE_BALANCE_JOB, SHADOW) //load balancing
FUNCTION_TYPE(EXT_FILE_REFRESH_JOB, SHADOW) //external table refresh
FUNCTION_TYPE(VECTOR_INDEX_REFRESH_JOB, SHADOW) //vector index refresh
FUNCTION_TYPE(DATA_DICT_DUMP_JOB, SHADOW) //data dictionary dump
FUNCTION_TYPE(FLUSH_NCOMP_DLL_JOB, SHADOW) // flush ncomp dll
FUNCTION_TYPE(POLLING_ASK_JOB_FOR_PL_RECOMPILE, SHADOW) // Polling check if PL recompile task needs to be started
FUNCTION_TYPE(DYNAMIC_PARTITION_MANAGE_JOB, SHADOW) // Scheduled dynamic partition management
