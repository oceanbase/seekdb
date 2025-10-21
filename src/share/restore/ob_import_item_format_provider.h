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

#ifndef OCEANBASE_SHARE_IMPORT_ITEM_FORMAT_PROVIEDER_H_
#define OCEANBASE_SHARE_IMPORT_ITEM_FORMAT_PROVIEDER_H_

#include <stdint.h>
#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace share
{
// Format string used for show to user
class ObIImportItemFormatProvider
{
public:
  virtual int64_t get_format_serialize_size() const = 0;

  virtual int format_serialize(char *buf, const int64_t buf_len, int64_t &pos) const = 0;

  int format_serialize(common::ObIAllocator &allocator, common::ObString &str) const;
};


// Format string used to persist to table
class ObIImportItemHexFormatProvider
{
public:
  virtual int64_t get_hex_format_serialize_size() const = 0;
  
  virtual int hex_format_serialize(char *buf, const int64_t buf_len, int64_t &pos) const = 0;

  virtual int hex_format_deserialize(const char *buf, const int64_t data_len, int64_t &pos) = 0;
};


class ObImportItemHexFormatImpl : public ObIImportItemHexFormatProvider
{
  OB_UNIS_VERSION_PV(); // pure virtual
public:
  virtual int64_t get_hex_format_serialize_size() const override;

  virtual int hex_format_serialize(char *buf, const int64_t buf_len, int64_t &pos) const override;

  virtual int hex_format_deserialize(const char *buf, const int64_t data_len, int64_t &pos) override;
};

}
}
#endif
