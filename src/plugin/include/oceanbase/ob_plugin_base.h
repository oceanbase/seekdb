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

#pragma once

#include <stdint.h>
#include <stddef.h>

#define OBP_PUBLIC_API __attribute__((visibility("default")))

#ifdef __cplusplus
#define OBP_PLUGIN_EXPORT extern "C" OBP_PUBLIC_API
#else // __cplusplus
#define OBP_PLUGIN_EXPORT OBP_PUBLIC_API
#endif // __cplusplus

#define OBP_STRINGIZE_(str) #str
#define OBP_STRINGIZE(str) OBP_STRINGIZE_(str)

#define OBP_DYNAMIC_PLUGIN_NAME_VAR               _ob_plugin_name
#define OBP_DYNAMIC_PLUGIN_API_VERSION_VAR        _ob_plugin_interface_version
#define OBP_DYNAMIC_PLUGIN_SIZEOF_VAR             _ob_plugin_sizeof
#define OBP_DYNAMIC_PLUGIN_PLUGIN_VAR             _ob_plugin
#define OBP_DYNAMIC_PLUGIN_BUILD_REVISION_VAR     _ob_plugin_build_revision
#define OBP_DYNAMIC_PLUGIN_BUILD_BRANCH_VAR       _ob_plugin_build_branch
#define OBP_DYNAMIC_PLUGIN_BUILD_DATE_VAR         _ob_plugin_build_date
#define OBP_DYNAMIC_PLUGIN_BUILD_TIME_VAR         _ob_plugin_build_time

#define OBP_DYNAMIC_PLUGIN_NAME_NAME              OBP_STRINGIZE(OBP_DYNAMIC_PLUGIN_NAME_VAR)
#define OBP_DYNAMIC_PLUGIN_API_VERSION_NAME       OBP_STRINGIZE(OBP_DYNAMIC_PLUGIN_API_VERSION_VAR)
#define OBP_DYNAMIC_PLUGIN_SIZEOF_NAME            OBP_STRINGIZE(OBP_DYNAMIC_PLUGIN_SIZEOF_VAR)
#define OBP_DYNAMIC_PLUGIN_PLUGIN_NAME            OBP_STRINGIZE(OBP_DYNAMIC_PLUGIN_PLUGIN_VAR)
#define OBP_DYNAMIC_PLUGIN_BUILD_REVISION_NAME    OBP_STRINGIZE(OBP_DYNAMIC_PLUGIN_BUILD_REVISION_VAR)
#define OBP_DYNAMIC_PLUGIN_BUILD_BRANCH_NAME      OBP_STRINGIZE(OBP_DYNAMIC_PLUGIN_BUILD_BRANCH_VAR)
#define OBP_DYNAMIC_PLUGIN_BUILD_DATE_NAME        OBP_STRINGIZE(OBP_DYNAMIC_PLUGIN_BUILD_DATE_VAR)
#define OBP_DYNAMIC_PLUGIN_BUILD_TIME_NAME        OBP_STRINGIZE(OBP_DYNAMIC_PLUGIN_BUILD_TIME_VAR)

#define OBP_BUILTIN_PLUGIN_VAR(name) ob_builtin_plugin_##name

#ifndef BUILD_REVISION
#define OBP_BUILD_REVISION ""
#else
#define OBP_BUILD_REVISION BUILD_REVISION
#endif

#ifndef BUILD_BRANCH
#define OBP_BUILD_BRANCH ""
#else
#define OBP_BUILD_BRANCH BUILD_BRANCH
#endif

#define OBP_BUILD_DATE __DATE__
#define OBP_BUILD_TIME __TIME__

/**
 * The maximum number of each field of version
 * @details Please refer to `OBP_MAKE_VERSION` for details
 * @NOTE don't touch me
 */
#define OBP_VERSION_FIELD_NUMBER 1000L

/**
 * Used for param type
 */
typedef void * ObPluginDatum;
