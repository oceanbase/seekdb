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

#include "common/number/ob_number_v2.h"

namespace oceanbase { namespace common {
class ObIAllocator;
class ObNumberConstValue
{
public:
  ObNumberConstValue() {}
  ~ObNumberConstValue() {}
  static int init(ObIAllocator &allocator);

public:
  static number::ObNumber MYSQL_MIN[number::ObNumber::MAX_PRECISION + 1][number::ObNumber::MAX_SCALE + 1];
  static number::ObNumber MYSQL_MAX[number::ObNumber::MAX_PRECISION + 1][number::ObNumber::MAX_SCALE + 1];
  static number::ObNumber MYSQL_CHECK_MIN[number::ObNumber::MAX_PRECISION + 1][number::ObNumber::MAX_SCALE + 1];
  static number::ObNumber MYSQL_CHECK_MAX[number::ObNumber::MAX_PRECISION + 1][number::ObNumber::MAX_SCALE + 1];
};

} }
