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

 #include "lib/oblog/ob_log_print_kv.h"
 #include "common/ob_tablet_id.h"

 namespace oceanbase
 {
 namespace storage
 {
 class ObDDLIncLogBasic final
 {
   OB_UNIS_VERSION_V(1);
 public:
   ObDDLIncLogBasic();
   ~ObDDLIncLogBasic() = default;
   int init(const ObTabletID &tablet_id,
            const ObTabletID &lob_meta_tablet_id);
   uint64_t hash() const;
   int hash(uint64_t &hash_val) const;
   void reset()
   {
     tablet_id_.reset();
     lob_meta_tablet_id_.reset();
   }
   bool operator ==(const ObDDLIncLogBasic &other) const
   {
     return tablet_id_ == other.get_tablet_id() && lob_meta_tablet_id_ == other.get_lob_meta_tablet_id();
   }
   bool is_valid() const { return tablet_id_.is_valid(); }
   const ObTabletID &get_tablet_id() const { return tablet_id_; }
   const ObTabletID &get_lob_meta_tablet_id() const { return lob_meta_tablet_id_; }
   TO_STRING_KV(K_(tablet_id), K_(lob_meta_tablet_id));
 private:
   ObTabletID tablet_id_;
   ObTabletID lob_meta_tablet_id_;
 };

 class ObDDLIncStartLog final
 {
   OB_UNIS_VERSION_V(1);
 public:
   ObDDLIncStartLog();
   ~ObDDLIncStartLog() = default;
   int init(const ObDDLIncLogBasic &log_basic);
   bool is_valid() const { return log_basic_.is_valid(); }
   const ObDDLIncLogBasic &get_log_basic() const { return log_basic_; }
   TO_STRING_KV(K_(log_basic));
 private:
   ObDDLIncLogBasic log_basic_;
 };

 class ObDDLIncCommitLog final
 {
   OB_UNIS_VERSION_V(1);
 public:
   ObDDLIncCommitLog();
   ~ObDDLIncCommitLog() = default;
   int init(const ObDDLIncLogBasic &log_basic);
   bool is_valid() const { return log_basic_.is_valid(); }
   const ObDDLIncLogBasic &get_log_basic() const { return log_basic_; }
   TO_STRING_KV(K_(log_basic));
 private:
   ObDDLIncLogBasic log_basic_;
 };

 } // namespace storage
 } // namespace oceanbase
