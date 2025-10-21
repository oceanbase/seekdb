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

#ifndef _LIBOBTABLE_H
#define _LIBOBTABLE_H 1

// all interface headers
#include "ob_table_service_client.h"
#include "share/table/ob_table.h"
#include "ob_hkv_table.h"
#include "ob_pstore.h"

/** @mainpage libobtable Documentation
 *
 * There are four public interface classes:
 *
 * 1. ObTable
 * 2. ObKVTable
 * 3. ObHKVTable
 * 4. ObTableServiceClient
 *
 */
namespace oceanbase
{
namespace table
{
/// Library entry class
class ObTableServiceLibrary
{
public:
  /**
   * Initialize the library.
   * @note must by called in single thread environment and before calling all other APIs
   *
   * @return error code
   */
  static int init();
  static void destroy();
};
} // end namespace table
} // end namespace oceanbase
#endif /* _LIBOBTABLE_H */
