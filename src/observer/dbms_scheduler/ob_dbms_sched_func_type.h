/**
 * Copyright (c) 2023 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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