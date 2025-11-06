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


// [0, 100) for inner group
// CGID_DEF(group_name, group_id[, flags=DEFAULT][, worker_concurrency=1])
// example: CGID_DEF(OBCG_EXAMPLE1, 1, CRITICAL)
//          CGID_DEF(OBCG_EXAMPLE2, 2, DEFAULT, 4)
// flags option: 
//     DEFAULT. No flags.
//     CRITICAL. If a group is not critical, the thread num of it can be set to 0 when idle. 
CGID_DEF(OBCG_DEFAULT, 0)
CGID_DEF(OBCG_CLOG, 1, DEFAULT, 1, NORMAL_EXPAND, false)
CGID_DEF(OBCG_ELECTION, 2, CRITICAL, 1, NORMAL_EXPAND, false)
CGID_DEF(OBCG_ID_SERVICE, 5, CRITICAL)
CGID_DEF(OBCG_DETECT_RS, 9, CRITICAL)
CGID_DEF(OBCG_LOC_CACHE, 10, CRITICAL)
CGID_DEF(OBCG_SQL_NIO, 11, DEFAULT, 1, NORMAL_EXPAND, false)
CGID_DEF(OBCG_MYSQL_LOGIN, 12)
CGID_DEF(OBCG_CDCSERVICE, 13, DEFAULT, 1, NORMAL_EXPAND, false)
CGID_DEF(OBCG_DIAG_TENANT, 14)
CGID_DEF(OBCG_WR, 15, CRITICAL)
CGID_DEF(OBCG_TRANSFER, 16)
CGID_DEF(OBCG_STORAGE_STREAM, 17, DEFAULT, 1, QUICK_EXPAND, false)
CGID_DEF(OBCG_DBA_COMMAND, 18, DEFAULT, 0)
CGID_DEF(OBCG_STORAGE, 19, DEFAULT, 1, NORMAL_EXPAND, false)
CGID_DEF(OBCG_LOCK, 20, DEFAULT, 2)
CGID_DEF(OBCG_UNLOCK, 21, DEFAULT, 2)
CGID_DEF(OBCG_DIRECT_LOAD_HIGH_PRIO, 22, DEFAULT, 1, NORMAL_EXPAND, false)
CGID_DEF(OBCG_DDL, 23)
CGID_DEF(OBCG_USER_LOCK, 24, DEFAULT, 1, NORMAL_EXPAND, false)
CGID_DEF(OBCG_HB_SERVICE, 25, CRITICAL)
CGID_DEF(OBCG_OLAP_ASYNC_JOB, 26)
CGID_DEF(OBCG_DTL, 27, CRITICAL)
CGID_DEF(OBCG_DBMS_SCHED_JOB, 28)
// 100 for CG_LQ
CGID_DEF(OBCG_LQ, 100)
//Lightweight transformation removes some groups:
