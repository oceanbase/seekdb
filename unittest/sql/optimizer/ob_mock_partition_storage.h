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

#ifndef _OB_MOCK_PARTITION_STORAGE_H_
#define _OB_MOCK_PARTITION_STORAGE_H_


namespace test
{
class MockPartitionStorage
{
public:
  MockPartitionStorage()
      : is_default_stat_(false)
  { }
  virtual ~MockPartitionStorage() { }
  void set_default_stat(const bool is_default_stat)
  {
    is_default_stat_ = is_default_stat;
  }
private:
  bool is_default_stat_;
};
}


#endif
