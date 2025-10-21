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

#include "lib/string/ob_string.h"
#include "lib/container/ob_array.h"

namespace oceanbase {
namespace plugin {

class ObPluginLoadOption final
{
public:
  enum ObOptionEnum
  {
    INVALID,
    ON,        // only print warning log
    OFF,       // disabled
    MAX,
  };

public:
  ObPluginLoadOption() = default;
  explicit ObPluginLoadOption(ObOptionEnum value) : value_(value)
  {}

  ObOptionEnum value() const { return value_; }

  const char * value_string() const;
  DECLARE_TO_STRING;

  static ObPluginLoadOption from_string(const common::ObString &str);

private:
  ObOptionEnum value_ = ObPluginLoadOption::INVALID;
};

struct ObPluginLoadParam
{
  common::ObString   library_name;
  ObPluginLoadOption load_option;

  TO_STRING_KV(K(library_name), K(load_option));
};

class ObPluginLoadParamParser
{
public:
  static int parse(const common::ObString &param, common::ObIArray<ObPluginLoadParam> &plugins_load);

private:
  static int parse_item(const common::ObString &item, ObPluginLoadParam &plugin_load);
};

} // namespace plugin
} // namespace oceanbase
