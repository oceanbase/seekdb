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

#ifndef OCEANBASE_STORAGE_OB_TABLET_DUMP_MDS_NODE_OPERATOR
#define OCEANBASE_STORAGE_OB_TABLET_DUMP_MDS_NODE_OPERATOR

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace storage
{
namespace mds
{
struct MdsDumpKV;
}

class ObTabletMdsData;

class ObTabletDumpMdsNodeOperator
{
public:
  ObTabletDumpMdsNodeOperator(ObTabletMdsData &mds_data, common::ObIAllocator &allocator);
public:
  int operator()(const mds::MdsDumpKV &kv);
  bool dumped() const { return dumped_; }
private:
  template <typename K, typename T>
  int dump(const mds::MdsDumpKV &kv, bool &dumped);
private:
  ObTabletMdsData &mds_data_;
  common::ObIAllocator &allocator_;
  bool dumped_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_DUMP_MDS_NODE_OPERATOR
