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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/ob_table_load_time_convert.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace sql;

ObTableLoadTimeConverter::ObTableLoadTimeConverter()
  : is_inited_(false)
{
}

ObTableLoadTimeConverter::~ObTableLoadTimeConverter()
{
}

int ObTableLoadTimeConverter::init(const ObString &format)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    ObTime ob_time;
    ob_time.mode_ |= DT_TYPE_DATETIME;
    if (ob_is_otimestamp_type(ObDateTimeType)) {
      ob_time.mode_ |= DT_TYPE_NANOSECOND;
    }
    if (OB_FAIL(ObDFMUtil::parse_datetime_format_string(format, dfm_elems_))) {
      LOG_WARN("fail to parse datetime format string", KR(ret), K(format));
    } else if (OB_FAIL(ObDFMUtil::check_semantic(dfm_elems_, elem_flags_, ob_time.mode_))) {
      LOG_WARN("check semantic of format string failed", KR(ret), K(format));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
