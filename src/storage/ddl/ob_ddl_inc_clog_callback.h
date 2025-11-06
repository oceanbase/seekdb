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

 #include "storage/ddl/ob_ddl_clog.h"
 #include "storage/ddl/ob_ddl_struct.h"
 #include "storage/ddl/ob_ddl_inc_clog.h"
 #include "storage/meta_mem/ob_tablet_handle.h"

 namespace oceanbase
 {
 namespace storage
 {

 class ObDDLIncClogCb : public logservice::AppendCb
 {
 public:
   ObDDLIncClogCb()
   : status_() {}
   virtual ~ObDDLIncClogCb() = default;
   virtual int on_success() override = 0;
   virtual int on_failure() override = 0;
   virtual void try_release() = 0;
   inline bool is_success() const { return status_.is_success(); }
   inline bool is_failed() const { return status_.is_failed(); }
   inline bool is_finished() const { return status_.is_finished(); }
   share::SCN get_scn() const { return __get_scn(); }
   int get_ret_code() const { return status_.get_ret_code(); }
 protected:
   ObDDLClogCbStatus status_;
 };

 class ObDDLIncStartClogCb : public ObDDLIncClogCb
 {
 public:
   ObDDLIncStartClogCb();
   virtual ~ObDDLIncStartClogCb() = default;
   int init(const share::ObLSID &ls_id, const ObDDLIncLogBasic &log_basic);
   virtual int on_success() override;
   virtual int on_failure() override;
   virtual void try_release() override;
   const char *get_cb_name() const override { return "DDLIncStartClogCb"; }
   INHERIT_TO_STRING_KV("ObDDLIncClogCb", ObDDLIncClogCb, K(is_inited_), K(ls_id_), K(log_basic_));
 private:
   bool is_inited_;
   share::ObLSID ls_id_;
   ObDDLIncLogBasic log_basic_;
 };

 class ObDDLIncRedoClogCb : public ObDDLIncClogCb
 {
 public:
   ObDDLIncRedoClogCb();
   virtual ~ObDDLIncRedoClogCb();
   int init(const share::ObLSID &ls_id,
            const storage::ObDDLMacroBlockRedoInfo &redo_info,
            const blocksstable::MacroBlockId &macro_block_id,
            storage::ObTabletHandle &tablet_handle);
   virtual int on_success() override;
   virtual int on_failure() override;
   virtual void try_release() override;
   const char *get_cb_name() const override { return "DDLIncRedoClogCb"; }
 private:
   bool is_inited_;
   share::ObLSID ls_id_;
   storage::ObDDLMacroBlockRedoInfo redo_info_;
   blocksstable::MacroBlockId macro_block_id_;
   ObSpinLock data_buffer_lock_;
   bool is_data_buffer_freed_;
   storage::ObTabletHandle tablet_handle_;
 };

 class ObDDLIncCommitClogCb : public ObDDLIncClogCb
 {
 public:
   ObDDLIncCommitClogCb();
   virtual ~ObDDLIncCommitClogCb() = default;
   int init(const share::ObLSID &ls_id, const ObDDLIncLogBasic &log_basic);
   virtual int on_success() override;
   virtual int on_failure() override;
   virtual void try_release() override;
   const char *get_cb_name() const override { return "DDLIncCommitClogCb"; }
   INHERIT_TO_STRING_KV("ObDDLIncClogCb", ObDDLIncClogCb, K(is_inited_), K(ls_id_), K(log_basic_));
 private:
   bool is_inited_;
   share::ObLSID ls_id_;
   ObDDLIncLogBasic log_basic_;
 };

 } // namespace storage
 } // namespace oceanbase
