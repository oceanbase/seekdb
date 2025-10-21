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

#ifndef __SHARE_OB_ODPS_CATALOG_UTILS_H__
#define __SHARE_OB_ODPS_CATALOG_UTILS_H__

#ifdef OB_BUILD_CPP_ODPS
#include <odps/odps_api.h>
#endif

namespace oceanbase
{
namespace sql
{
class ObODPSGeneralFormat;
}

namespace share
{
class ObODPSCatalogProperties;

class ObODPSCatalogUtils
{
public:
#ifdef OB_BUILD_CPP_ODPS
  static int create_odps_conf(const ObODPSCatalogProperties &odps_format, apsara::odps::sdk::Configuration &conf);
#endif
};

} // namespace share
} // namespace oceanbase

#endif // __SHARE_OB_ODPS_CATALOG_UTILS_H__
