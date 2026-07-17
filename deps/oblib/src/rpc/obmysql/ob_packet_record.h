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

#ifndef _OB_MYSQL_OB_PACKET_RECORD_H_
#define _OB_MYSQL_OB_PACKET_RECORD_H_
#include "rpc/obmysql/ob_mysql_packet.h"

namespace oceanbase
{
namespace obmysql
{

bool enable_proto_dia();

struct ResRecordFlags {
  uint8_t is_send_: 1; // 0-send, 1-receive
  uint8_t processed_: 1; // Request processing finished, this bit will be marked after sending the packet.
  uint8_t reservered_: 8; // The remaining bits are used for special markers
};

/*
  // for send packet
  // 0-> mysql packet; 1->okp;
  // 2->error packet; 3->eof packet
  // 4-> row packet; 5-> field packet;
  // 6->piece packet; 7-> string packet;
  // 8-> prepare packet; 9 ->result header packet
  // 10-> prepare execute packet;

  // for recieve packet
  this field represents packet command
*/
struct ObpMysqHeader {
  union {
    uint32_t len_;
    uint32_t pkt_num_; // indicates the number of row packets/field packets.
  } mysql_header_;
  uint32_t rec_; // indicates how many bytes of the mysql packet have been received.
  uint32_t com_len_; // compress head len
  uint8_t seq_;
  uint8_t type_;
  uint8_t com_seq_; // compress head sequence
  uint8_t is_send_:1;
  uint8_t is_file_content_:1;
  ObpMysqHeader() {
    rec_ = 0;
    seq_ = 0;
    mysql_header_.len_ = 0;
    is_file_content_ = 0;
  }
  ~ObpMysqHeader() {}

  bool is_com_pkt_valid() const {
    return com_len_ != 0 && com_seq_ != 0;
  }
  TO_STRING_KV(K_(mysql_header_.len), K_(rec), K_(seq));
}; // 16byte

class ObPacketRecord
{
public:
  ObPacketRecord() {
    obp_mysql_header_.type_ = 0;
    obp_mysql_header_.is_send_ = 0;
  }
  ~ObPacketRecord() {}

  //for mysql fragment
  inline void record_recieve_mysql_pkt_fragment(int32_t rec) __restrict__ {
    obp_mysql_header_.rec_ += rec;
  }
  //for mysql fragment end

  // for mysql protocol
  inline void record_recieve_mysql_packet(ObMySQLRawPacket &__restrict__ pkt) __restrict__ 
  {
    obp_mysql_header_.mysql_header_.len_ = pkt.get_pkt_len();
    obp_mysql_header_.seq_ = pkt.get_seq();
    obp_mysql_header_.type_ = static_cast<uint8_t>(pkt.get_cmd()); 
    obp_mysql_header_.is_send_ = 0;
  }
  inline void record_send_mysql_packet(ObMySQLPacket &__restrict__ pkt, int32_t len) __restrict__ 
  {
    if (pkt.get_mysql_packet_type() == ObMySQLPacketType::PKT_ROW ||
        pkt.get_mysql_packet_type() == ObMySQLPacketType::PKT_FIELD) {
      obp_mysql_header_.mysql_header_.pkt_num_++;
    } else {
      obp_mysql_header_.mysql_header_.len_ = len;
    }
    obp_mysql_header_.seq_ = pkt.get_seq();
    obp_mysql_header_.type_ = static_cast<uint8_t>(pkt.get_mysql_packet_type()); 
    obp_mysql_header_.is_send_ = 1;
  }
  // for mysql protocol end 

  // for compress mysql protocol
  inline void record_send_comp_packet(uint32_t com_len, uint8_t com_seq) __restrict__  {
    obp_mysql_header_.com_len_ = com_len;
    obp_mysql_header_.com_seq_ = com_seq;
  }
  inline void record_recieve_comp_packet(ObMySQLCompressedPacket &com_pkt) __restrict__  {
    obp_mysql_header_.com_len_ = com_pkt.get_comp_len();
    obp_mysql_header_.com_seq_ = com_pkt.get_comp_seq();
  }
  // for compress mysql protocol end 

  inline bool is_send_record() const __restrict__  {
    return obp_mysql_header_.is_send_ == 1;
  }

  inline void set_packet_type(ObMySQLPacketType type) __restrict__ {
    obp_mysql_header_.type_ = static_cast<uint8_t>(type);
  }

  inline void set_file_content() __restrict__ {
    obp_mysql_header_.is_file_content_ = 1;
  }

  int64_t to_string(char *buf, const int64_t buf_len) const;
  ObpMysqHeader obp_mysql_header_;   // 16 byte

}__attribute((aligned(32)));; // end of class ObPacketRecord

class ObPacketRecordWrapper {
  public:
    static const int64_t REC_BUF_SIZE = 32;
    ObPacketRecordWrapper() {
      start_pkt_pos_ = 0;
      cur_pkt_pos_ = 0;
      last_type_ = obmysql::ObMySQLPacketType::INVALID_PKT;
      enable_proto_dia_ = false;
      receiving_file_contents_ = false;
    }
    ~ObPacketRecordWrapper() {}
    void init() {
      start_pkt_pos_ = 0;
      cur_pkt_pos_ = 0;
      last_type_ = obmysql::ObMySQLPacketType::INVALID_PKT;
      enable_proto_dia_ = obmysql::enable_proto_dia();
      receiving_file_contents_ = false;
    }
    int64_t to_string(char *buf, int64_t buf_len) const;

    // for compress protocol
    inline void begin_seal_comp_pkt() { start_pkt_pos_ = cur_pkt_pos_; }
    inline void end_seal_comp_pkt(uint32_t com_len, uint8_t com_seq)
    {
      for (int64_t i = start_pkt_pos_;  i < cur_pkt_pos_; i++) {
        int64_t idx = i % ObPacketRecordWrapper::REC_BUF_SIZE;
        obmysql::ObPacketRecord& rec = pkt_rec_[idx];
        rec.record_send_comp_packet(com_len, com_seq);
      }
    }
    void record_recieve_comp_packet(ObMySQLCompressedPacket &com_pkt,
                                                            obmysql::ObMySQLRawPacket &pkt)
    {
      int64_t idx = cur_pkt_pos_ % ObPacketRecordWrapper::REC_BUF_SIZE;
      obmysql::ObPacketRecord& rec = pkt_rec_[idx];
      rec.record_recieve_comp_packet(com_pkt);
      rec.record_recieve_mysql_packet(pkt);
      cur_pkt_pos_++;

      if (OB_UNLIKELY(receiving_file_contents_)) {
        pkt_rec_[idx].set_file_content();
        if (0 == pkt.get_clen()) {
          receiving_file_contents_ = false;
        }
      }
    }
    // for compress protocol end 


    // for mysql protocol
    inline void record_send_mysql_pkt(obmysql::ObMySQLPacket &__restrict__ pkt, int32_t len) __restrict__ 
    {
      if (pkt.get_mysql_packet_type() == last_type_) {
        // do nothing
      } else {
        cur_pkt_pos_++;
      }
      int64_t idx = (cur_pkt_pos_-1) % ObPacketRecordWrapper::REC_BUF_SIZE;
      pkt_rec_[idx].record_send_mysql_packet(pkt, len);
      last_type_ = pkt.get_mysql_packet_type();

      if (OB_UNLIKELY(pkt.get_mysql_packet_type() == ObMySQLPacketType::PKT_FILENAME)) {
        receiving_file_contents_ = true;
      }
    }
    inline void record_recieve_mysql_packet(obmysql::ObMySQLRawPacket &__restrict__ pkt) __restrict__ 
    {
      int64_t idx = cur_pkt_pos_ % ObPacketRecordWrapper::REC_BUF_SIZE;
      pkt_rec_[idx].record_recieve_mysql_packet(pkt);
      cur_pkt_pos_++;

      if (OB_UNLIKELY(receiving_file_contents_)) {
        pkt_rec_[idx].set_file_content();
        if (0 == pkt.get_clen()) {
          receiving_file_contents_ = false;
        }
      }
    }
    inline void record_recieve_mysql_pkt_fragment(int32_t recive) __restrict__ 
    {
      int64_t idx = cur_pkt_pos_ % ObPacketRecordWrapper::REC_BUF_SIZE;
      pkt_rec_[idx].record_recieve_mysql_pkt_fragment(recive);
    }
    // for mysql protocol end

    inline bool enable_proto_dia() {
      return enable_proto_dia_;
    }
  public:
    obmysql::ObPacketRecord pkt_rec_[REC_BUF_SIZE];
    uint32_t start_pkt_pos_;
    uint32_t cur_pkt_pos_;
    obmysql::ObMySQLPacketType last_type_;
    bool enable_proto_dia_;
    // in load local infile, we will receive some file content packets and there is no `cmd` in the packet.
    // so we use a flag to mark the context.
    bool receiving_file_contents_;
};



} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OB_MYSQL_OB_PACKET_RECORD_H_ */
