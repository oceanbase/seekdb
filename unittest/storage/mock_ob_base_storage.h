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

#include <gmock/gmock.h>
#include "storage/ob_base_storage.h"

namespace oceanbase
{
namespace storage
{

class MockObBaseStorage : public ObBaseStorage
{
public:
  MockObBaseStorage() {}
  virtual ~MockObBaseStorage() {}
  MOCK_METHOD0(get_data_file, blocksstable::ObDataFile & ());
  MOCK_METHOD0(get_commit_logger, blocksstable::ObBaseStorageLogger & ());
  MOCK_METHOD0(write_check_point, int ());
};


}  // namespace storage
}  // namespace oceanbase

