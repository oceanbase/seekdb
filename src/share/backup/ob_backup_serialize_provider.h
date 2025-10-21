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

#ifndef OCEANBASE_SHARE_OB_BACKUP_SERIALIZE_PROVIEDER_H_
#define OCEANBASE_SHARE_OB_BACKUP_SERIALIZE_PROVIEDER_H_

#include "share/backup/ob_backup_struct.h"

namespace oceanbase
{
namespace share
{

class ObIBackupSerializeProvider
{
  OB_UNIS_VERSION_PV(); // pure virtual
public:
  virtual ~ObIBackupSerializeProvider() {}

  virtual bool is_valid() const = 0;
  // Get file data type
  virtual uint16_t get_data_type() const = 0;
  // Get file data version
  virtual uint16_t get_data_version() const = 0;
  virtual uint16_t get_compressor_type() const = 0;

  DECLARE_PURE_VIRTUAL_TO_STRING;
};


// Wrapper backup serialize data with common backup header.
class ObBackupSerializeHeaderWrapper final : public ObIBackupSerializeProvider
{
public:
  // the only constructor.
  explicit ObBackupSerializeHeaderWrapper(ObIBackupSerializeProvider *serializer)
      : serializer_(serializer) {}
  virtual ~ObBackupSerializeHeaderWrapper() {}

  bool is_valid() const override;
  uint16_t get_data_type() const override;
  uint16_t get_data_version() const override;
  uint16_t get_compressor_type() const override;

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const override;
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos) override;
  int64_t get_serialize_size() const override;

  TO_STRING_KV(K_(*serializer));

private:
  ObIBackupSerializeProvider *serializer_;
};

}
}

#endif
