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

#include "lib/utility/ob_macro_utils.h"
#include "share/backup/ob_backup_struct.h"

namespace oceanbase
{
namespace sql
{
const char *const LOAD_DATA_FORMAT = "load_data_format=";

struct ObLoadDataFormat
{
public:
  ObLoadDataFormat() {}
  ~ObLoadDataFormat() {}

#define LOAD_DATA_FORMAT_DEF(DEF) \
  DEF(INVALID_FORMAT, = 0)        \
  DEF(CSV, )                      \
  DEF(OB_BACKUP_1_4, )            \
  DEF(OB_BACKUP_3_X, )            \
  DEF(OB_BACKUP_2_X_LOG, )        \
  DEF(OB_BACKUP_2_X_PHY, )        \
  DEF(MAX_FORMAT, )

  DECLARE_ENUM(Type, type, LOAD_DATA_FORMAT_DEF, static);
};

class ObLoadDataStorageInfo : public share::ObBackupStorageInfo
{
public:
  using common::ObObjectStorageInfo::set;

public:
  ObLoadDataStorageInfo();
  virtual ~ObLoadDataStorageInfo();

  virtual int assign(const ObLoadDataStorageInfo &storage_info);
  bool is_valid() const override;
  void reset() override;

#define DEFINE_GETTER_AND_SETTER(type, name)            \
  OB_INLINE type get_##name() const { return name##_; } \
  OB_INLINE void set_##name(type name) { name##_ = name; }

  DEFINE_GETTER_AND_SETTER(ObLoadDataFormat::Type, load_data_format);

#undef DEFINE_GETTER_AND_SETTER

  int set(const common::ObStorageType device_type, const char *storage_info) override;
  int get_storage_info_str(char *storage_info, const int64_t info_len) const override;

  INHERIT_TO_STRING_KV("ObObjectStorageInfo", ObObjectStorageInfo, "load_data_format",
                       ObLoadDataFormat::get_type_string(load_data_format_));

private:
  int parse_load_data_params(const char *storage_info);
  int set_load_data_param_defaults();

private:
  ObLoadDataFormat::Type load_data_format_;
};

} // namespace sql
} // namespace oceanbase
