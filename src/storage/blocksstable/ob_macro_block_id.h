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

#ifndef SRC_STORAGE_BLOCKSSTABLE_OB_MACRO_BLOCK_ID_H_
#define SRC_STORAGE_BLOCKSSTABLE_OB_MACRO_BLOCK_ID_H_

#include "share/ob_define.h"
#include "lib/restore/ob_io_device.h"

namespace oceanbase
{
namespace blocksstable
{

class MacroBlockId final
{
public:
  MacroBlockId();
  MacroBlockId(const uint64_t write_seq, const int64_t block_index, const int64_t third_id);
  MacroBlockId(const MacroBlockId &id);
  ~MacroBlockId() = default;

  OB_INLINE void reset()
  {
    first_id_ = 0;
    second_id_ = INT64_MAX;
    third_id_ = 0;
    version_ = MACRO_BLOCK_ID_VERSION;
  }
  bool is_valid() const;

  uint64_t hash() const;
  int hash(uint64_t &hash_val) const;
  int64_t to_string(char *buf, const int64_t buf_len) const;
  void first_id_to_string(char *buf, const int64_t buf_len, int64_t &pos) const;

  int64_t first_id() const { return first_id_; }
  int64_t second_id() const { return second_id_; }
  int64_t third_id() const { return third_id_; }
  void set_first_id(const int64_t first_id) { first_id_ = first_id; }
  void set_second_id(const int64_t second_id) { second_id_ = second_id; }
  void set_third_id(const int64_t third_id) { third_id_ = third_id; }

  int64_t block_index() const { return block_index_; }
  void set_block_index(const int64_t block_index) { block_index_ = block_index; }
  uint64_t write_seq() const { return write_seq_; }
  void set_write_seq(const uint64_t write_seq) { write_seq_ = write_seq; }

  void set_from_io_fd(const common::ObIOFd &block_id)
  {
    first_id_ = block_id.first_id_;
    second_id_ = block_id.second_id_;
    third_id_ = block_id.third_id_;
  }

  MacroBlockId &operator=(const MacroBlockId &other)
  {
    first_id_ = other.first_id_;
    second_id_ = other.second_id_;
    third_id_ = other.third_id_;
    return *this;
  }
  bool operator==(const MacroBlockId &other) const;
  bool operator!=(const MacroBlockId &other) const;
  bool operator<(const MacroBlockId &other) const;

  NEED_SERIALIZE_AND_DESERIALIZE;
  static MacroBlockId mock_valid_macro_id() { return MacroBlockId(0, AUTONOMIC_BLOCK_INDEX, 0); }

public:
  static constexpr int64_t MACRO_BLOCK_ID_VERSION = 1;
  static constexpr int64_t EMPTY_ENTRY_BLOCK_INDEX = -1;
  static constexpr int64_t AUTONOMIC_BLOCK_INDEX = -1;
  static constexpr uint64_t SF_BIT_WRITE_SEQ = 60;
  static constexpr uint64_t SF_BIT_VERSION = 4;
#ifndef _WIN32
  static constexpr uint64_t MAX_WRITE_SEQ = (0x1UL << MacroBlockId::SF_BIT_WRITE_SEQ) - 1;
#else
  static constexpr uint64_t MAX_WRITE_SEQ = (UINT64_C(0x1) << MacroBlockId::SF_BIT_WRITE_SEQ) - 1;
#endif

private:
  static constexpr uint64_t HASH_MAGIC_NUM = 2654435761;
  union {
    int64_t first_id_;
    struct {
      uint64_t write_seq_ : SF_BIT_WRITE_SEQ;
      uint64_t version_ : SF_BIT_VERSION;
    };
  };
  union {
    int64_t second_id_;
    int64_t block_index_;
  };
  int64_t third_id_;
};

OB_INLINE bool MacroBlockId::operator==(const MacroBlockId &other) const
{
  return first_id_ == other.first_id_ && second_id_ == other.second_id_ && third_id_ == other.third_id_;
}

OB_INLINE bool MacroBlockId::operator!=(const MacroBlockId &other) const
{
  return !(other == *this);
}

#define SERIALIZE_MEMBER_WITH_MEMCPY(member)                                 \
  if (OB_SUCC(ret)) {                                                        \
    if (OB_UNLIKELY(buf_len - pos < sizeof(member))) {                       \
      ret = OB_BUF_NOT_ENOUGH;                                               \
      LOG_WARN("buffer not enough", K(ret), KP(buf), K(buf_len), K(pos));    \
    } else {                                                                 \
      MEMCPY(buf + pos, &member, sizeof(member));                            \
      pos += sizeof(member);                                                 \
    }                                                                        \
  }

#define DESERIALIZE_MEMBER_WITH_MEMCPY(member)                               \
  if (OB_SUCC(ret)) {                                                        \
    if (OB_UNLIKELY(data_len - pos < sizeof(member))) {                      \
      ret = OB_DESERIALIZE_ERROR;                                            \
      LOG_WARN("buffer not enough", K(ret), KP(buf), K(data_len), K(pos));   \
    } else {                                                                 \
      MEMCPY(&member, buf + pos, sizeof(member));                            \
      pos += sizeof(member);                                                 \
    }                                                                        \
  }

} // namespace blocksstable
} // namespace oceanbase

#endif /* SRC_STORAGE_BLOCKSSTABLE_OB_MACRO_BLOCK_ID_H_ */
