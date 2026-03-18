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

#ifndef OCEANBASE_ARCHIVE_OB_LS_META_RECORDER_H_
#define OCEANBASE_ARCHIVE_OB_LS_META_RECORDER_H_

#include <cstdint>
#include "lib/hash/ob_link_hashmap.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "ob_archive_define.h"
#include "share/backup/ob_archive_struct.h"
#include "share/ob_ls_id.h"                    // ObLSID
#include "share/scn.h"                    // ObLSID
namespace oceanbase
{
namespace archive
{
typedef common::LinkHashValue<share::ObArchiveLSMetaType> RecordContextValue;
class ObArchiveRoundMgr;
struct RecordContext : public RecordContextValue
{
  share::SCN last_record_scn_;
  int64_t last_record_round_;
  int64_t last_record_piece_;
  int64_t last_record_file_;

  RecordContext();
  ~RecordContext();

  int set(const RecordContext &other);
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(last_record_scn), K_(last_record_round),
      K_(last_record_piece), K_(last_record_file));
};

typedef common::ObLinkHashMap<share::ObArchiveLSMetaType, RecordContext> RecordContextMap;
class ObLSMetaRecorder
{
public:
  ObLSMetaRecorder();
  ~ObLSMetaRecorder();

public:
  int init(ObArchiveRoundMgr *round_mgr);
  void destroy();
  void handle();

private:
  int check_and_get_record_context_(const share::ObArchiveLSMetaType &type, RecordContext &context);
  int insert_or_update_record_context_(const share::ObArchiveLSMetaType &type, RecordContext &record_context);
  void clear_record_context_();
  int prepare_();
  bool check_need_delay_(const share::ObLSID &id, const ArchiveKey &key, const share::SCN &ts);
  int build_path_(const share::ObLSID &id,
      const ArchiveKey &key,
      const share::SCN &scn,
      const share::ObArchiveLSMetaType &type,
      const int64_t file_id,
      share::ObBackupPath &path,
      RecordContext &record_context);
  int generate_common_header_(char *buf,
      const int64_t header_size,
      const int64_t data_size,
      const share::SCN &scn);
  int do_record_(const ArchiveKey &key, const char *buf, const int64_t size, share::ObBackupPath &path);
  int make_dir_(const share::ObLSID &id,
      const ArchiveKey &key,
      const share::SCN &scn,
      const share::ObArchiveLSMetaType &type);
  void clear_();

private:
  bool inited_;
  ObArchiveRoundMgr *round_mgr_;
  char *buf_;
  RecordContextMap map_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObLSMetaRecorder);
};

} // namespace archive
} // namespace oceanbase

#endif /* OCEANBASE_ARCHIVE_OB_LS_META_RECORDER_H_ */
