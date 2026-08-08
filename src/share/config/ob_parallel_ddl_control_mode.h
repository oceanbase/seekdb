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

#ifndef OCEANBASE_SHARE_CONFIG_OB_PARALLEL_DDL_CONTROL_MODE_H_
#define OCEANBASE_SHARE_CONFIG_OB_PARALLEL_DDL_CONTROL_MODE_H_

#include "lib/string/ob_sql_string.h"
#include "share/config/ob_config.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

class ObParallelDDLControlMode final : public common::ObIConfigMode
{
public:
  ObParallelDDLControlMode(): value_(0) {}
  enum ObParallelDDLType {
    TRUNCATE_TABLE = 0,
    SET_COMMENT = 1,
    CREATE_INDEX = 2,
    CREATE_VIEW = 3,
    DROP_TABLE = 4,
    MAX_TYPE // can not > 32
  };

  static constexpr uint64_t MASK_SIZE = 2;
  static constexpr uint64_t MASK = 0x03;
  virtual int set_value(const common::ObConfigModeItem &mode_item) override;
  uint64_t get_value() const { return value_; }
  int set_parallel_ddl_mode(const ObParallelDDLType type, const uint8_t mode);
  int is_parallel_ddl(const ObParallelDDLType type, bool &is_parallel);
  static int is_parallel_ddl_enable(const ObParallelDDLType ddl_type, bool &is_parallel);
  static int string_to_ddl_type(const common::ObString &ddl_string, ObParallelDDLType &ddl_type);
  static int generate_parallel_ddl_control_config_for_create_tenant(common::ObSqlString &config_value);
private:
  bool check_mode_valid_(uint8_t mode) { return mode > MASK ? false : true; }
  uint64_t value_;
  DISALLOW_COPY_AND_ASSIGN(ObParallelDDLControlMode);
};

} // namespace schema
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_PARALLEL_DDL_CONTROL_MODE_H_
