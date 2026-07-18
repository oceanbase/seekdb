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

#ifndef OCEANBASE_STORAGE_DICT_OB_IK_UTF8_DICT_H_
#define OCEANBASE_STORAGE_DICT_OB_IK_UTF8_DICT_H_
#include "storage/fts/dict/ob_dic_loader.h"

namespace oceanbase
{
namespace storage
{
class ObTenantIKUTF8DicLoader final : public ObTenantDicLoader
{
public:
  ObTenantIKUTF8DicLoader()  = default;
  virtual ~ObTenantIKUTF8DicLoader() = default;
  virtual int init() override;
private:
  virtual int get_dic_item(const uint64_t i, const uint64_t pos, ObDicItem& item) override;
  virtual int fill_dic_item(const ObDicItem &item, share::ObDMLSqlSplicer &dml) override;
  virtual ObDicTableInfo get_main_dic_info() override;
  virtual ObDicTableInfo get_stop_dic_info() override;
  virtual ObDicTableInfo get_quantifier_dic_info() override;
  DISALLOW_COPY_AND_ASSIGN(ObTenantIKUTF8DicLoader);
};
} //end storage
} // end oceanbase
#endif //OCEANBASE_STORAGE_DICT_OB_IK_UTF8_DICT_H_
