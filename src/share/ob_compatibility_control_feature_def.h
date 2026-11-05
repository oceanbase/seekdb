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

// define the feature list here whose behavior is different in MySQL5.7, MySQL8.0 or OB
// DEF_COMPAT_CONTROL_FEATURE(type, id, is_dynanmic, description, lastest_version, patch_versions ...)

#ifdef DEF_COMPAT_CONTROL_FEATURE
DEF_COMPAT_CONTROL_FEATURE(FUNC_REPLACE_NULL,
    "The result of REPLACE('abd', '', null) is different in MySQL 5.7 and 8.0",
    CLUSTER_VERSION_1_0_0_0)

DEF_COMPAT_CONTROL_FEATURE(UPD_LIMIT_OFFSET,
    "MySQL do not support the use of OFFSET in the LIMIT clause of UPDATE/DELETE statement",
    CLUSTER_VERSION_1_0_0_0)

DEF_COMPAT_CONTROL_FEATURE(PROJECT_NULL,
    "MySQL will rename the projection item names with pure null values to `NULL`",
    CLUSTER_VERSION_1_0_0_0)

DEF_COMPAT_CONTROL_FEATURE(VAR_NAME_LENGTH,
    "MySQL will limit the length of user-defined variable names to within 64 characters",
    CLUSTER_VERSION_1_0_0_0)

DEF_COMPAT_CONTROL_FEATURE(NULL_VALUE_FOR_CLOSED_CURSOR,
    "Return null value to client for closed cursor, indicating that the cursor is not open",
    CLUSTER_VERSION_1_0_0_0)

DEF_COMPAT_CONTROL_FEATURE(FUNC_LOCATE_NULL,
    "The result of REPLACE('x', 'abc', null) is different in MySQL 5.7 and 8.0",
    CLUSTER_VERSION_1_0_0_0)

DEF_COMPAT_CONTROL_FEATURE(INVOKER_RIGHT_COMPILE,
    "Use the definer's database during compile, regardless of whether is definer or invoker right",
    CLUSTER_VERSION_1_0_0_0)

DEF_COMPAT_CONTROL_FEATURE(RECV_PLAIN_PASSWORD,
    "The set password command is different in different OceanBase versions",
    CLUSTER_VERSION_1_0_0_0)

DEF_COMPAT_CONTROL_FEATURE(OUT_ANONYMOUS_COLLECTION_IS_ALLOW,
    "The output parameter is returned according to the original input param \n"
    "type which is not empty for inout anonymous array which used in anonymous block",
    CLUSTER_VERSION_1_0_0_0)
#endif
