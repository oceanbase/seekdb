/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "ob_mysql_request_utils.h"
#include "lib/compress/zlib/ob_zlib_compressor.h"
#include "rpc/ob_request.h"
#include "rpc/obmysql/ob_mysql_util.h"
#include "rpc/obmysql/obsm_struct.h"

using namespace oceanbase::common;

void __attribute__((weak)) request_finish_callback() {}

namespace oceanbase
{
namespace obmysql
{

ObMySQLRequestUtils::ObMySQLRequestUtils(){}

ObMySQLRequestUtils::~ObMySQLRequestUtils(){}

static int64_t get_max_comp_pkt_size(const int64_t uncomp_pkt_size)
{
  int64_t ret_size = 0;
  if (uncomp_pkt_size > MAX_COMPRESSED_BUF_SIZE) {
    //limit max comp_buf_size is 2M-1k
    ret_size = MAX_COMPRESSED_BUF_SIZE;
  } else {
    ret_size = (common::OB_MYSQL_COMPRESSED_HEADER_SIZE
                + uncomp_pkt_size
                + 13
                + (uncomp_pkt_size >> 12)
                + (uncomp_pkt_size >> 14)
                + (uncomp_pkt_size >> 25));
    if (ret_size > MAX_COMPRESSED_BUF_SIZE) {
      ret_size = MAX_COMPRESSED_BUF_SIZE;
    }
  }
  return ret_size;
}

/*
 * when use compress, packet header looks like:
 *  3B  length of compressed payload
 *  1B  compressed sequence id
 *  3B  length of payload before compression
 *
 *  the body is compressed packet(orig header + orig body)
 *
 * NOTE: In standard mysql compress protocol, if src_pktlen < 50B, or compr_pktlen >= src_pktlen
 *       mysql will do not compress it and set pktlen_before_compression = 0,
 *       it can not ensure checksum.
 * NOTE: In OB, we need always checksum ensured first!
 */
static int build_compressed_packet(ObEasyBuffer &src_buf,
    const int64_t next_compress_size, ObCompressionContext &context)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(context.send_buf_)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "send_buf_ is null", K(context), K(ret));
  } else if (OB_ISNULL(context.conn_)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "conn_ is null", K(context), K(ret));
  } else {
    ObEasyBuffer dst_buf(*context.send_buf_);
    const int64_t comp_buf_size = dst_buf.write_avail_size() - OB_MYSQL_COMPRESSED_HEADER_SIZE;
    ObZlibCompressor compressor;
    bool use_real_compress = true;
    if (context.use_checksum()) {
      int64_t com_level = context.conn_->proxy_cap_flags_.is_ob_protocol_v2_compress() ? 6 : 0;
      compressor.set_compress_level(com_level);
      use_real_compress = !context.is_checksum_off_;
    }
    int64_t dst_data_size = 0;
    int64_t pos = 0;
    int64_t len_before_compress = 0;
    if (use_real_compress) {
      if (OB_FAIL(compressor.compress(src_buf.read_pos(), next_compress_size,
                                      dst_buf.last() + OB_MYSQL_COMPRESSED_HEADER_SIZE,
                                      comp_buf_size, dst_data_size))) {
        SERVER_LOG(WARN, "compress packet failed", K(ret));
      } else if (OB_UNLIKELY(dst_data_size > comp_buf_size)) {
        ret = OB_SIZE_OVERFLOW;
        SERVER_LOG(WARN, "dst_data_size is overflow, it should not happened",
                   K(dst_data_size), K(comp_buf_size), K(ret));
      } else {
        len_before_compress = next_compress_size;
      }
    } else if (next_compress_size > comp_buf_size) {
      ret = OB_BUF_NOT_ENOUGH;
      SERVER_LOG(WARN, "do not use real compress, dst buffer is not enough", K(ret),
                 K(next_compress_size), K(comp_buf_size), K(lbt()));
    } else {
      //if compress off, just copy date to output buf
      MEMCPY(dst_buf.last() + OB_MYSQL_COMPRESSED_HEADER_SIZE, src_buf.read_pos(), next_compress_size);
      dst_data_size = next_compress_size;
      len_before_compress = 0;
    }

    if (FAILEDx(ObMySQLUtil::store_int3(dst_buf.last(), OB_MYSQL_COMPRESSED_HEADER_SIZE,
                                               static_cast<int32_t>(dst_data_size), pos))) {
      SERVER_LOG(WARN, "failed to store_int3", K(ret));
    } else if (OB_FAIL(ObMySQLUtil::store_int1(dst_buf.last(), OB_MYSQL_COMPRESSED_HEADER_SIZE,
                                               context.seq_, pos))) {
      SERVER_LOG(WARN, "failed to store_int1", K(ret));
    } else if (OB_FAIL(ObMySQLUtil::store_int3(dst_buf.last(), OB_MYSQL_COMPRESSED_HEADER_SIZE,
                                               static_cast<int32_t>(len_before_compress), pos))) {
      SERVER_LOG(WARN, "failed to store_int3", K(ret));
    } else {
      if (context.conn_->pkt_rec_wrapper_.enable_proto_dia()) {
        context.conn_->pkt_rec_wrapper_.end_seal_comp_pkt(
                          static_cast<uint32_t>(dst_data_size), context.seq_);
      }
      SERVER_LOG(DEBUG, "succ to build compressed pkt", "comp_len", dst_data_size,
                 "comp_seq", context.seq_, K(len_before_compress), K(next_compress_size),
                 K(src_buf), K(dst_buf), K(context), K(context.conn_->sessid_));
      src_buf.read(next_compress_size);
      dst_buf.write(dst_data_size + OB_MYSQL_COMPRESSED_HEADER_SIZE);
      ++context.seq_;
    }
  }
  return ret;
}

static int build_compressed_buffer(ObEasyBuffer &orig_send_buf,
    ObCompressionContext &context)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(context.conn_)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "conn_ is null", K(context), K(ret));
  } else if (NULL != context.send_buf_) {
    ObEasyBuffer comp_send_buf(*context.send_buf_);
    if (OB_UNLIKELY(!orig_send_buf.is_valid()) || OB_UNLIKELY(!comp_send_buf.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(ERROR, "orig_send_buf or comp_send_buf is invalid", K(orig_send_buf), K(comp_send_buf), K(ret));
    } else {
      const int64_t max_read_step = context.get_max_read_step();
      bool is_v2_compress = context.conn_->proxy_cap_flags_.is_ob_protocol_v2_compress();
      char * proxy_pos = is_v2_compress ? NULL : context.last_pkt_pos_;
      int64_t next_read_size = orig_send_buf.get_next_read_size(proxy_pos, max_read_step);
      int64_t last_read_size = 0;
      if (next_read_size > (comp_send_buf.write_avail_size() - OB_MYSQL_COMPRESSED_HEADER_SIZE)) {
        next_read_size = max_read_step;
      }
      int64_t max_comp_pkt_size = get_max_comp_pkt_size(next_read_size);
      while (OB_SUCC(ret)
             && next_read_size > 0
             && max_comp_pkt_size <= comp_send_buf.write_avail_size()) {
        // v2 compress protocol not do this check
        //error+ok/ok packet should use last seq
        if (!is_v2_compress && context.last_pkt_pos_ == orig_send_buf.read_pos() && context.is_proxy_compress_based()) {
          --context.seq_;
        }

        if (OB_FAIL(build_compressed_packet(orig_send_buf, next_read_size, context))) {
          SERVER_LOG(WARN, "fail to build_compressed_packet", K(ret));
        } else {
          //optimize for multi packet
          last_read_size = next_read_size;
          next_read_size = orig_send_buf.get_next_read_size(proxy_pos, max_read_step);
          if (next_read_size > (comp_send_buf.write_avail_size() - OB_MYSQL_COMPRESSED_HEADER_SIZE)) {
            next_read_size = max_read_step;
          }
          if (last_read_size != next_read_size) {
            max_comp_pkt_size = get_max_comp_pkt_size(next_read_size);
          }
        }
      }
    }
  }
  return ret;
}

static int reuse_compress_buffer(ObCompressionContext &comp_context, ObEasyBuffer &orig_send_buf,
                                                  int64_t comp_buf_size, rpc::ObRequest &req)
{
  int ret = OB_SUCCESS;
  bool need_alloc = false;
  if (NULL == comp_context.send_buf_) {
    need_alloc = true;
    //use buf_size to avoid alloc again next time
    comp_buf_size = get_max_comp_pkt_size(orig_send_buf.orig_buf_size());
  } else {
    const int64_t new_size = get_max_comp_pkt_size(orig_send_buf.read_avail_size());
    if (new_size <= comp_buf_size) {
      //reusing last size is enough
    } else {
      SERVER_LOG(DEBUG, "need resize compressed buf", "old_size", comp_buf_size, K(new_size),
                 "orig_send_buf_", orig_send_buf);
      //realloc
      comp_buf_size = new_size;
      need_alloc = true;
    }
  }

  //alloc if necessary
  if (need_alloc) {
    const uint32_t size = static_cast<uint32_t>(comp_buf_size + sizeof(easy_buf_t));
    if (OB_ISNULL(comp_context.send_buf_ =
        reinterpret_cast<easy_buf_t *>(SQL_REQ_OP.alloc_sql_response_buffer(&req, size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SERVER_LOG(ERROR, "allocate memory failed", K(size), K(ret));
    }
  }

  //reuse
  if (OB_SUCC(ret)) {
    init_easy_buf(comp_context.send_buf_,
                  reinterpret_cast<char *>(comp_context.send_buf_ + 1),
                  NULL, comp_buf_size);
  }

  return ret;
}

static int send_compressed_buffer(bool pkt_has_completed, ObCompressionContext &comp_context, 
                                                            ObEasyBuffer &orig_send_buf, rpc::ObRequest &req)
{
  int ret = OB_SUCCESS;
  const bool is_last_flush = pkt_has_completed && !orig_send_buf.is_read_avail();
  int64_t buf_size = 0;
  int64_t read_avail_size = 0;

  if (NULL != comp_context.send_buf_) {
    ObEasyBuffer comp_send_buf(*comp_context.send_buf_);
    buf_size = comp_send_buf.orig_buf_size();
    read_avail_size = comp_send_buf.read_avail_size();
  }

  if (read_avail_size > 0) {
    if (is_last_flush) {
        if (OB_FAIL(SQL_REQ_OP.async_write_response(&req, comp_context.send_buf_->pos, read_avail_size))) {
          SERVER_LOG(WARN, "failed to flush buffer", K(ret));
        }
    } else {
      if (OB_FAIL(SQL_REQ_OP.write_response(&req, comp_context.send_buf_->pos, read_avail_size))) {
        SERVER_LOG(WARN, "failed to flush buffer", K(ret));
      }
    }
  }

  if (OB_SUCC(ret) && !is_last_flush) {
    if (OB_FAIL(reuse_compress_buffer(comp_context, orig_send_buf, buf_size, req))) {
      SERVER_LOG(WARN, "faild to reuse_compressed_buffer_sql_nio", K(ret));
    }
  }

  return ret;
}

int ObMySQLRequestUtils::flush_buffer(ObFlushBufferParam &param)
{
  int ret = OB_NOT_SUPPORTED;
  SERVER_LOG(ERROR, "not supported, should not be here", K(ret));
  return ret;
}

int ObMysqlPktContext::save_fragment_mysql_packet(const char *start, const int64_t len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(start) || len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "invalid input value", KP(start), K(len), K(ret));
  } else {
    if (payload_buf_alloc_len_ >= (len + payload_buffered_total_len_)) {
      // buffer is enough
    } else {
      int64_t alloc_size = std::max(len, payload_len_);
      if (alloc_size > OB_MYSQL_MAX_PAYLOAD_LENGTH) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(ERROR, "invalid alloc size", K(alloc_size), K(ret));
      } else {
        ObMemAttr attr;
        attr.label_ = "LibMultiPackets";
        attr.ctx_id_ = ObCtxIds::DEFAULT_CTX_ID;
        attr.tenant_id_ = tenant_id_;
        alloc_size += payload_buffered_total_len_;
        char *tmp_buffer = reinterpret_cast<char *>(ob_malloc(alloc_size, attr));
        if (OB_ISNULL(tmp_buffer)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          SERVER_LOG(ERROR, "fail to alloc memory", K(alloc_size), K(ret));
        } else {
          if (payload_buffered_total_len_ > 0) { // skip the first alloc
            MEMCPY(tmp_buffer, payload_buf_, payload_buffered_total_len_);
          }
          payload_buf_alloc_len_ = alloc_size;
          if (OB_NOT_NULL(payload_buf_)) {
            ob_free(payload_buf_);
          }
          payload_buf_ = tmp_buffer;
        }
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_UNLIKELY(payload_buf_alloc_len_ < (payload_buffered_total_len_ + len))) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(ERROR, "invalid buf len", K_(payload_buf_alloc_len),
                  K_(payload_buffered_total_len), K(len), K(ret));
      } else {
        MEMCPY(payload_buf_ + payload_buffered_total_len_, start, len);
        payload_buffered_total_len_ += len;
        payload_buffered_len_ += len;
      }
    }
  }

  return ret;
}

int ObMySQLRequestUtils::flush_compressed_buffer(bool pkt_has_completed, ObCompressionContext &comp_context, 
                                                              ObEasyBuffer &orig_send_buf, rpc::ObRequest &req)
{
  int ret = OB_SUCCESS;
  int64_t need_hold_size = 0;


  if (OB_ISNULL(comp_context.conn_)) {
    ret = OB_INVALID_ARGUMENT;
    SERVER_LOG(WARN, "conn_ is null", K(comp_context), K(ret));
  } else if (comp_context.conn_->proxy_cap_flags_.is_ob_protocol_v2_compress()){
    // do nothing
  } else if (comp_context.need_hold_last_pkt(pkt_has_completed)) {
    need_hold_size = orig_send_buf.proxy_read_avail_size(comp_context.last_pkt_pos_);
    orig_send_buf.write(0 - need_hold_size);
    SERVER_LOG(DEBUG, "need hold uncompleted proxy pkt", K(need_hold_size),
            "orig_send_buf", orig_send_buf);
  }

  if (false == orig_send_buf.is_read_avail()) {

  } else {
    while (OB_SUCC(ret)
            && orig_send_buf.is_read_avail()) {
      if (OB_FAIL(build_compressed_buffer(orig_send_buf, comp_context))) {
        SERVER_LOG(WARN, "failed to build_compressed_buffer", K(ret));
        break;
      } else if (OB_FAIL(send_compressed_buffer(pkt_has_completed, comp_context, orig_send_buf, req))) {
        SERVER_LOG(WARN, "faild to send compressed response", K(ret));
        break;
      }
    }
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (false == pkt_has_completed) {
    bool need_reset_last_pkt_pos = (comp_context.last_pkt_pos_ == orig_send_buf.last());
    init_easy_buf(&orig_send_buf.buf_, reinterpret_cast<char *>(&orig_send_buf.buf_ + 1),
                  NULL, orig_send_buf.orig_buf_size());

    // v2 compression not check
    if (comp_context.conn_->proxy_cap_flags_.is_ob_protocol_v2_compress()) {
      need_reset_last_pkt_pos = false;
    }

    if (need_reset_last_pkt_pos) {
      if (need_hold_size > 0) {
          MEMMOVE(orig_send_buf.last(), comp_context.last_pkt_pos_, need_hold_size);
          orig_send_buf.write(need_hold_size);
      }
      comp_context.last_pkt_pos_ = orig_send_buf.begin();
      SERVER_LOG(DEBUG, "need reset last_pkt_pos", K(need_hold_size),
            "orig_send_buf_", orig_send_buf,
            "comp_context", comp_context);
    }
  }
  
  return ret;
}

} //end of namespace obmysql
} //end of namespace oceanbase
