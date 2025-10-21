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

#ifndef OCEANBASE_STORAGE_OB_SERVER_SLOG_WRITER
#define OCEANBASE_STORAGE_OB_SERVER_SLOG_WRITER

#include "storage/slog/ob_storage_log_writer.h"

namespace oceanbase
{
namespace storage
{

class ObServerSlogWriter : public ObStorageLogWriter
{
public:
  ObServerSlogWriter() {}
  virtual ~ObServerSlogWriter() {}
  virtual void wait() override;
protected:
  virtual int start() override;
};

}
}

#endif
