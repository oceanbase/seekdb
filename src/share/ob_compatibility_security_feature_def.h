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
DEF_COMPAT_CONTROL_FEATURE(MYSQL_PRIV_ENHANCE, "add privilege check to some command",
    CLUSTER_VERSION_1_0_0_0)
DEF_COMPAT_CONTROL_FEATURE(MYSQL_SET_VAR_PRIV_ENHANCE, "check privilege for set var subquery",
    CLUSTER_VERSION_1_0_0_0)
DEF_COMPAT_CONTROL_FEATURE(MYSQL_USER_REVOKE_ALL_ENHANCE, "use create_user to check privilege for revoke all from user",
    CLUSTER_VERSION_1_0_0_0)
DEF_COMPAT_CONTROL_FEATURE(MYSQL_LOCK_TABLES_PRIV_ENHANCE, "add privilege for lock tables",
    CLUSTER_VERSION_1_0_0_0)
DEF_COMPAT_CONTROL_FEATURE(MYSQL_USER_REVOKE_ALL_WITH_PL_PRIV_CHECK, "revoke all on db.* need check pl privilege",
    CLUSTER_VERSION_1_0_0_0)
DEF_COMPAT_CONTROL_FEATURE(MYSQL_REFERENCES_PRIV_ENHANCE, "add privilege check to references",
    CLUSTER_VERSION_1_0_0_0)
DEF_COMPAT_CONTROL_FEATURE(MYSQL_TRIGGER_PRIV_CHECK, "add trigger privilege check",
    CLUSTER_VERSION_1_0_0_0)
DEF_COMPAT_CONTROL_FEATURE(MYSQL_EVENT_PRIV_CHECK, "add event privilege check",
    CLUSTER_VERSION_1_0_0_0)
#endif
