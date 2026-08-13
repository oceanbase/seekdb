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

#ifndef OCEANBASE_SHARE_TABLET_OB_TABLET_READ_MODE_H_
#define OCEANBASE_SHARE_TABLET_OB_TABLET_READ_MODE_H_

namespace oceanbase
{
namespace storage
{

// Tablet creation and deletion are transactional MDS operations. These modes
// describe which committed state a caller may observe through the shared DDL
// interface. The namespace is retained because Storage implements the reads;
// the vocabulary is owned by Share because it appears in Share's interface.
enum class ObMDSGetTabletMode
{
  READ_ALL_COMMITED = 0,
  READ_WITHOUT_CHECK = 1,
  READ_READABLE_COMMITED = 2,
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_SHARE_TABLET_OB_TABLET_READ_MODE_H_
