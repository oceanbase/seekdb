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

#ifndef _OB_PSTORE_H
#define _OB_PSTORE_H 1
#include "ob_hkv_table.h"
namespace oceanbase
{
namespace table
{
/// Interface for PStore.
class ObPStore
{
public:
  ObPStore();
  virtual ~ObPStore();

  int init(ObTableServiceClient &client);
  void destroy();

  int get(const ObString &table_name, const ObString &column_family, const ObHKVTable::Key &key, ObHKVTable::Value &value);
  int multi_get(const ObString &table_name, const ObString &column_family, const ObHKVTable::IKeys &keys, ObHKVTable::IValues &values);

  int put(const ObString &table_name, const ObString &column_family, const ObHKVTable::Key &key, const ObHKVTable::Value &value);
  int multi_put(const ObString &table_name, const ObString &column_family, const ObHKVTable::IKeys &keys, const ObHKVTable::IValues &values);

  int remove(const ObString &table_name, const ObString &column_family, const ObHKVTable::Key &key);
  int multi_remove(const ObString &table_name, const ObString &column_family, const ObHKVTable::IKeys &keys);
private:
  DISALLOW_COPY_AND_ASSIGN(ObPStore);
private:
  // data members
  bool inited_;
  ObTableServiceClient *client_;
};

/**
 * @example ob_pstore_example.cpp
 * This is an example of how to use the ObPStore class.
 *
 */
} // end namespace table
} // end namespace oceanbase

#endif /* _OB_PSTORE_H */
