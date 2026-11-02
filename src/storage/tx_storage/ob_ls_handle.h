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

#ifndef OCEANBASE_STORAGE_TX_STORAGE_OB_LS_HANDLE
#define OCEANBASE_STORAGE_TX_STORAGE_OB_LS_HANDLE

#include "lib/utility/ob_print_utils.h"
#include "share/leak_checker/obj_leak_checker.h"
#include "storage/ls/ob_ls_get_mod.h"

namespace oceanbase
{
namespace storage
{
class ObLSMap;
class ObLS;

class ObLSHandle final
{
public:
  ObLSHandle();
  ObLSHandle(const ObLSHandle &other);
  ObLSHandle &operator=(const ObLSHandle &other);
  ~ObLSHandle();
  int set_ls(const ObLSMap &ls_map, ObLS &ls, const ObLSGetMod &mod);
  void reset();
  bool is_valid() const;
  ObLS *get_ls() { return ls_; }
  ObLS *get_ls() const { return ls_; }
  TO_STRING_KV(KP(ls_map_), KP(ls_), K(mod_));
private:
  const ObLSMap *ls_map_;
  ObLS *ls_;
  ObLSGetMod mod_;
  DEFINE_OBJ_LEAK_DEBUG_NODE(node_);
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_TX_STORAGE_OB_LS_HANDLE
