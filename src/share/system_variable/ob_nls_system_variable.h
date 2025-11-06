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

#ifndef OCEANBASE_SQL_SESSION_OB_NLS_SYSTEM_VARIABLE_
#define OCEANBASE_SQL_SESSION_OB_NLS_SYSTEM_VARIABLE_

#include <utility>
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace share
{
class IsoCurrencyUtils {
public:
  typedef std::pair<const char*, const char*> CountryCurrencyPair;
  IsoCurrencyUtils() {}
  static int get_currency_by_country_name(const common::ObString &country_name,
                                          common::ObString &currency_name);
  static bool is_country_valid(const common::ObString &country_name);
  static const CountryCurrencyPair COUNTRY_CURRENCY_LIST_[];
};

}
}
#endif // OCEANBASE_SQL_SESSION_OB_NLS_SYSTEM_VARIABLE_
