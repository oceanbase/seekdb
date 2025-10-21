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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_CMD_OB_SYS_DISPATCH_CALL_CONFIG_H_
#define OCEANBASE_SRC_SQL_RESOLVER_CMD_OB_SYS_DISPATCH_CALL_CONFIG_H_

/*
 * Whitelist of system package subprocedures that can be dispatched by the sys tenant
 * ENTRY FORMAT
 *    Each V(arg1, arg2) macro takes two arguments:
 *    - arg1: name of the system package (e.g. DBMS_SCHEDULER, DBMS_STATS).
 *    - arg2: name of the subprocedure within the package (e.g. SET_PARAM).
 * WILDCARD SUPPORT
 *    The * symbol as the second argument indicates that all subprocedures within the specified
 *    package are allowed.
 * EXAMPLES
 *    V(DBMS_SCHEDULER, *): allows all subprocedures in the DBMS_SCHEDULER package.
 *    V(DBMS_STATS, SET_PARAM): allows the set_param subprocedure in the DBMS_STATS package.
 */
#define SYS_DISPATCH_CALL_WHITELIST(V)      \
  V(DBMS_SCHEDULER, *)                      \
  V(DBMS_PARTITION, *)                      \
  V(DBMS_BALANCE, *)                        \
  V(DBMS_STATS, RESET_PARAM_DEFAULTS)       \
  V(DBMS_STATS, SET_PARAM)                  \
  V(DBMS_STATS, RESET_GLOBAL_PREF_DEFAULTS) \
  V(DBMS_STATS, SET_GLOBAL_PREFS)           \
  V(DBMS_STATS, SET_SCHEMA_PREFS)           \
  V(DBMS_STATS, SET_TABLE_PREFS)            \
  V(DBMS_STATS, DELETE_SCHEMA_PREFS)        \
  V(DBMS_STATS, DELETE_TABLE_PREFS)

#endif  // OCEANBASE_SRC_SQL_RESOLVER_CMD_OB_SYS_DISPATCH_CALL_CONFIG_H_
