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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_TASK_EVENT_
#define OCEANBASE_SQL_EXECUTOR_OB_TASK_EVENT_

#include "sql/executor/ob_task_location.h"
#include "share/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/allocator/page_arena.h"
#include "common/object/ob_object.h"
#include "sql/executor/ob_slice_id.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace sql
{
// Due to calling the rpc asynchronous callback interface, this interface does not call the destructor of the parameters,
// Therefore the following classes should be designed not to rely on the destructor for memory release

class ObTaskSmallResult
{
  OB_UNIS_VERSION(1);
public:
  const static int64_t MAX_DATA_BUF_LEN = 4 * 1024L; // 4k

  ObTaskSmallResult();
  virtual ~ObTaskSmallResult();

  void reset();
  bool equal(const ObTaskSmallResult &other) const;
  int assign(const ObTaskSmallResult &other);
  inline void set_has_data(bool has_data) { has_data_ = has_data; }
  inline bool has_data() const { return has_data_; }
  inline bool has_empty_data() const { return has_data_ && 0 == data_len_; }
  inline int64_t get_data_len() const { return data_len_; }
  inline void set_data_len(int64_t data_len) { data_len_ = data_len; }
  inline const char *get_data_buf() const { return data_buf_; }
  inline char *get_data_buf_for_update() { return data_buf_; }
  void set_found_rows(const int64_t count) { found_rows_ = count; }
  int64_t get_found_rows() const { return found_rows_; }
  void set_affected_rows(const int64_t count) { affected_rows_ = count; }
  int64_t get_affected_rows() const { return affected_rows_; }
  void set_last_insert_id(const int64_t id) { last_insert_id_ = id; }
  int64_t get_last_insert_id() const { return last_insert_id_; }
  void set_duplicated_rows(int64_t duplicated_rows) { duplicated_rows_ = duplicated_rows; }
  int64_t get_duplicated_rows() const { return duplicated_rows_; }
  void set_matched_rows(int64_t matched_rows) { matched_rows_ = matched_rows; }
  int64_t get_matched_rows() const { return matched_rows_; }
  TO_STRING_KV(K_(has_data),
               K_(data_len),
               K_(affected_rows),
               K_(found_rows),
               K_(last_insert_id),
               K_(matched_rows),
               K_(duplicated_rows));
private:
  bool has_data_;
  int64_t data_len_;
  char data_buf_[MAX_DATA_BUF_LEN];
  int64_t affected_rows_;
  int64_t found_rows_;
  int64_t last_insert_id_;
  int64_t matched_rows_;
  int64_t duplicated_rows_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTaskSmallResult);
};

enum ObShuffleType
{
  ST_NONE = 0,
  ST_HASH,
  ST_RANGE,
  ST_KEY,
  ST_LIST,
};

class ObSliceEvent final
{
  OB_UNIS_VERSION(1);
public:
  ObSliceEvent()
    : ob_slice_id_(),
      small_result_()
  {}

  // used for normal copy, such as array.puah_back().
  int assign(const ObSliceEvent &other);
  // used for deep copy.

  void set_ob_slice_id(const ObSliceID &ob_slice_id) { ob_slice_id_ = ob_slice_id; }
  const ObSliceID &get_ob_slice_id() const { return ob_slice_id_; }
  const ObTaskSmallResult &get_small_result() const { return small_result_; }
  ObTaskSmallResult &get_small_result_for_update() { return small_result_; }
  bool has_small_result() const { return small_result_.has_data(); }
  const char *get_small_result_buf() const { return small_result_.get_data_buf(); }
  int64_t get_small_result_len() const { return small_result_.get_data_len(); }
  bool has_data() const { return small_result_.has_data(); }
  bool has_empty_data() const { return small_result_.has_empty_data(); }
  int64_t get_found_rows() const { return small_result_.get_found_rows(); }
  int64_t get_affected_rows() const { return small_result_.get_affected_rows(); }
  int64_t get_matched_rows() const { return small_result_.get_matched_rows(); }
  int64_t get_duplicated_rows() const { return small_result_.get_duplicated_rows(); }

  TO_STRING_KV(K_(ob_slice_id),
               K_(small_result));

private:
  ObSliceID ob_slice_id_;
  ObTaskSmallResult small_result_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSliceEvent);
};

class ObTaskEvent
{
  OB_UNIS_VERSION(1);
public:
  ObTaskEvent();
  virtual ~ObTaskEvent();

  virtual void reset();

  inline const ObTaskLocation &get_task_location() const {return task_loc_;}
  inline int64_t get_err_code() const {return err_code_;}
  inline bool is_valid() const {return inited_ && task_loc_.is_valid();}
  inline void set_task_recv_done(int64_t ts)    { ts_task_recv_done_    = ts; }
  inline void set_result_send_begin(int64_t ts) { ts_result_send_begin_ = ts; }
  inline int64_t get_task_recv_done() const    { return ts_task_recv_done_; }
  inline int64_t get_result_send_begin() const { return ts_result_send_begin_; }
  TO_STRING_KV("task_loc", task_loc_,
               "err_code", err_code_,
               "inited", inited_);
protected:
  ObTaskLocation task_loc_;
  int64_t err_code_;
  bool inited_;
  int64_t ts_task_recv_done_;
  int64_t ts_result_send_begin_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTaskEvent);
};


}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_TASK_EVENT_ */
