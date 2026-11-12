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

// Load the host Google Test ABI before selecting the isolated policy's Windows
// branch. This Linux executable tests the Windows budget without a Windows SDK
// or Windows Google Test archive, and shares no translation unit with the
// native-policy executable.
#include <gtest/gtest.h>
#define _WIN32
#include "unittest/share/rs_timeout_policy_cases.h"
#undef _WIN32
