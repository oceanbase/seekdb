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

#ifndef OCEANBASE_SHARE_OB_ARCHIVE_PIECE_H_
#define OCEANBASE_SHARE_OB_ARCHIVE_PIECE_H_

#include "lib/ob_define.h"                 // int64_t..
#include "lib/utility/ob_print_utils.h"    // print
#include <cstdint>
#include "share/scn.h"    //SCN

namespace oceanbase
{
namespace share
{
/*
 * Archive data needs to support offline copying, directory management, etc., so it is necessary to determine which backup directory a piece of data belongs to during archiving.
 * In the 2.x/3.x scheme, rs schedules multiple observers to participate, and for each LogEntry, it must be clearly placed in a specific directory.
 * In 4.x, the observer determines the backup directory for a log file based on the time range of logs contained in the clog file, according to the directory management strategy.
 *
 * For log files spanning multiple directories, backups that satisfy the actual time constraints need to be made in the corresponding multiple directories, for example,
 * a file A contains logs spanning 3 days, 1d: {(log_id: 1-10)}, 2d: {(log_id: 11-15)}, 3d: {(log_id: 16-30)},
 *
 * The final presentation could be [d1:A(1-10)]/[d2:A(1-15)]/[d3:A(1-30)],
 * or [d1:A(1-30)]/[d2:A(1-30)]/[d3:A(1-30)]
 *
 * Formula: piece = (scn.convert_to_ts() - genesis_scn.convert_to_ts()) / interval + base_piece_id
 * where scn is the log submission scn, genesis_ts is the base, and interval is the piece time span, defaulting to one day.
 *
 * In actual production environments, the piece split unit is one day and is not displayed to users,
 * while test environments support finer granularity directory split units.
 *
 * NB: Piece split unit configuration can only be changed when archiving is not enabled; once enabled, it cannot be modified.
 * */
class ObArchivePiece final
{
public:
  //us
  const int64_t ONE_SECOND = 1000 * 1000L;
  const int64_t ONE_MINUTE = 60L * ONE_SECOND;
  const int64_t ONE_HOUR = 60L * ONE_MINUTE;
  const int64_t ONE_DAY = 24L * ONE_HOUR;

public:
  ObArchivePiece();
  ObArchivePiece(const SCN &scn, const int64_t interval_us, const SCN &genesis_scn, const int64_t base_piece_id);
  ~ObArchivePiece();

public:
  int64_t get_piece_id() const { return piece_id_; }
  int get_piece_lower_limit(share::SCN &scn);
  bool is_valid() const;
  void reset();
  int set(const int64_t piece_id, const int64_t interval_us, const SCN &genesis_scn, const int64_t base_piece_id);
  void inc();
  ObArchivePiece &operator=(const ObArchivePiece &other);
  ObArchivePiece &operator++();       // prefix ++
  bool operator==(const ObArchivePiece &other) const;
  bool operator!=(const ObArchivePiece &other) const;
  bool operator>(const ObArchivePiece &other) const;
  TO_STRING_KV(K_(interval_us), K_(genesis_scn), K_(piece_id), K_(base_piece_id));

private:
  int64_t        interval_us_;    // piece time length
  SCN      genesis_scn_;  // Archive base SCN
  int64_t        base_piece_id_; // base piece id
  int64_t        piece_id_;    // piece directory
};

} // namespace share
} // namespace oceanbase
#endif /* OCEANBASE_SHARE_OB_ARCHIVE_PIECE_H_ */
