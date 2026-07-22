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

#ifndef  _OB_MYSQL_REQUEST
#define  _OB_MYSQL_REQUEST
#include "io/easy_io.h"
#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/allocator/page_arena.h"
#include "rpc/obmysql/ob_mysql_packet.h"

namespace oceanbase
{
namespace common
{
class ObDataBuffer;
class ObArenaAllocator;
}
namespace rpc
{
class ObRequest;
}
namespace observer
{
class ObSMConnection;
}

namespace obmysql
{
class ObMySQLPacket;
class ObEasyBuffer;
class ObCompressionContext;

static const int64_t OB_MAX_COMPRESSED_PACKET_LENGTH = (1L << 20); //1M
static const int64_t MAX_COMPRESSED_BUF_SIZE = common::OB_MALLOC_BIG_BLOCK_SIZE;//2M-1k

class ObMysqlPktContext
{
public:
  enum ObMysqlPktReadStep {
    READ_HEADER = 0,
    READ_BODY,
    READ_COMPLETE
  };

  void reset_parameter()
  {
    static_assert(common::OB_MYSQL_HEADER_LENGTH == 4, "OB_MYSQL_HEADER_LENGTH != 4");
    *reinterpret_cast<uint32_t *>(header_buf_) = 0;
    header_buffered_len_ = 0;
    payload_buf_alloc_len_ = 0;
    payload_buffered_len_ = 0;
    payload_buffered_total_len_ = 0;
    payload_len_ = 0;
    last_pkt_seq_ = 0;
    curr_pkt_seq_ = 0;
    next_read_step_ = READ_HEADER;
    raw_pkt_.reset();
    is_multi_pkt_ = false;
    is_auth_switch_ = false;
  }

  ObMysqlPktContext()
  {
    reset_parameter();
    payload_buf_ = NULL;
    
  }

  ~ObMysqlPktContext()
  {
    if (NULL != payload_buf_) {
      ob_free(payload_buf_);
    }
    payload_buf_ = NULL;
  }

  void reset()
  {
    reset_parameter();
    if (NULL != payload_buf_) {
      ob_free(payload_buf_);
    }
    payload_buf_ = NULL;
  }

  int save_fragment_mysql_packet(const char *start, const int64_t len);

  static const char *get_read_step_str(const ObMysqlPktReadStep step)
  {
    switch (step) {
      case READ_HEADER:
        return "READ_HEADER";
      case READ_BODY:
        return "READ_BODY";
      case READ_COMPLETE:
        return "READ_COMPLETE";
      default:
        return "UNKNOWN";
    }
  }

  TO_STRING_KV(K_(header_buffered_len), K_(payload_buffered_len), K_(payload_buffered_total_len),
               K_(last_pkt_seq), K_(payload_len), K_(curr_pkt_seq), K_(payload_buf_alloc_len),
               "next_read_step", get_read_step_str(next_read_step_), K_(raw_pkt),
               "total_alloc_size", payload_buf_alloc_len_, K_(is_multi_pkt), K_(is_auth_switch));

public:
  char header_buf_[common::OB_MYSQL_HEADER_LENGTH];
  int64_t header_buffered_len_;
  char *payload_buf_;
  int64_t payload_buf_alloc_len_;
  int64_t payload_buffered_len_; // not include header
  int64_t payload_buffered_total_len_; // not include header
  int64_t payload_len_;
  uint8_t last_pkt_seq_;
  uint8_t curr_pkt_seq_;
  ObMysqlPktReadStep next_read_step_;
  ObMySQLRawPacket raw_pkt_;
  bool is_multi_pkt_;
  bool is_auth_switch_;
  

private:
  
  DISALLOW_COPY_AND_ASSIGN(ObMysqlPktContext);
};

class ObCompressedPktContext
{
public:
   ObCompressedPktContext() { reset(); }
   ~ObCompressedPktContext() { }
  void reset()
  {
    last_pkt_seq_ = 0;
    is_multi_pkt_ = false;
  }

  void reuse()
  {
    // keep the last_pkt_seq_ here
    is_multi_pkt_ = false;
  }

  TO_STRING_KV(K_(last_pkt_seq),
               K_(is_multi_pkt));

public:
  uint8_t last_pkt_seq_;
  bool is_multi_pkt_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObCompressedPktContext);
};

class ObEasyBuffer
{
public:
  explicit ObEasyBuffer(easy_buf_t &ezbuf) : buf_(ezbuf), read_pos_(ezbuf.pos) { }
  ~ObEasyBuffer() {}
  int64_t read_avail_size() const { return buf_.last - read_pos_; }
  int64_t write_avail_size() const { return buf_.end - buf_.last; }
  int64_t orig_data_size() const { return buf_.last - buf_.pos; }
  int64_t orig_buf_size() const { return buf_.end - buf_.pos; }
  bool is_valid() const { return (orig_buf_size() >= 0 && orig_data_size() >= 0); }
  bool is_read_avail() const { return buf_.last > read_pos_; }
  char *read_pos() const { return read_pos_; }
  char *begin() const { return buf_.pos; }
  char *last() const { return buf_.last; }
  char *end() const { return buf_.end; }
  void read(const int64_t size) { read_pos_ += size;}
  void write(const int64_t size) { buf_.last += size;}
  void fall_back(const int64_t size) { buf_.last -= size; }

  TO_STRING_KV(KP_(read_pos), KP(buf_.pos), KP(buf_.last), KP(buf_.end),
               "orig_buf_size", orig_buf_size(),
               "orig_data_size", orig_data_size(),
               "read_avail_size", read_avail_size(),
               "write_avail_size", write_avail_size());

public:
  easy_buf_t &buf_;
  char *read_pos_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObEasyBuffer);
};

enum ObCompressType
{
  NO_COMPRESS = 0,
  DEFAULT_COMPRESS, //compress the whole buf every 1M
};

class ObCompressionContext
{
public:
  ObCompressionContext() { reset(); }
  ~ObCompressionContext() {}

  void reset() { memset(this, 0, sizeof(ObCompressionContext)); }
  bool use_compress() const { return NO_COMPRESS != type_; }
  bool use_uncompress() const { return NO_COMPRESS == type_; }
  bool is_default_compress() const { return DEFAULT_COMPRESS == type_; }

  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    J_OBJ_START();
    J_KV(K_(sessid), K_(type), K_(seq), K_(comp_level));
    J_COMMA();
    if (NULL != send_buf_) {
      J_KV("send_buf", ObEasyBuffer(*send_buf_));
    } else {
      J_KV(KP_(send_buf));
    }
    J_OBJ_END();
    return pos;
  }

public:
  ObCompressType type_;
  uint8_t seq_;//compressed pkt seq
  easy_buf_t *send_buf_;
  uint32_t sessid_;
  observer::ObSMConnection *conn_;
  int64_t comp_level_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObCompressionContext);
};

class ObMySQLRequestUtils
{
public:
  ObMySQLRequestUtils();
  virtual ~ObMySQLRequestUtils();

  static int flush_compressed_buffer(bool pkt_has_completed, ObCompressionContext &comp_context, 
                                                  ObEasyBuffer &orig_send_buf, rpc::ObRequest &req);
private:
  DISALLOW_COPY_AND_ASSIGN(ObMySQLRequestUtils);
};

} //end of namespace obmysql
} //end of namespace oceanbase

extern void request_finish_callback();

#endif
