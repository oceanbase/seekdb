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

#define USING_LOG_PREFIX SHARE
#include "query/vector/ob_vector_index_serialize.h"
#include "share/ob_lob_access_utils.h"
#include "share/rc/ob_server_runtime.h"
#include "observer/vector_index/ob_vector_index_util.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "storage/tx_storage/ob_access_service.h"
#include "query/vector/ob_vector_index_adaptor.h"

namespace oceanbase
{
namespace share
{
/*
 * ObOStreamBuf implement
 * */
std::streamsize ObOStreamBuf::xsputn(const char* s, std::streamsize count)
{
  std::streamsize written_size = 0;
  std::streamsize left_size = 0;
  if (count == 0) {
    // do nothing
  } else if (OB_ISNULL(s)) {
    last_error_code_ = OB_INVALID_ARGUMENT;
  }
  while (is_valid() && is_success() && written_size < count) {
    left_size = epptr() - pptr();
    std::streamsize sub_size = std::min(count - written_size, left_size);
    MEMCPY(pptr(), s + written_size, sub_size);
    pbump(static_cast<int>(sub_size));
    written_size += sub_size;
    if (written_size < count) {
      last_error_code_ = do_callback();
    }
  }
  return written_size;
}

ObOStreamBuf::int_type ObOStreamBuf::overflow(int_type ch)
{
  if (is_valid() && is_success()) {
    if (ch != traits_type::eof()) {
      *pptr() = traits_type::to_char_type(ch);
      pbump(1);
    }
    last_error_code_ = do_callback();
  }
  return ch;
}

int ObOStreamBuf::do_callback()
{
  int ret = OB_SUCCESS;
  int64_t data_size = pptr() - pbase();
  if (0 < data_size) {
    if (OB_FAIL(cb_(pbase(), data_size, cb_param_))) {
    } else {
      setp(data_, data_ + capacity_ - 1); // reset to clear write buffer
    }
  }
  return ret;
}

void ObOStreamBuf::check_finish()
{
  if (is_valid() && is_success()) {
    last_error_code_ = do_callback();
  }
}

/*
 * ObIStreamBuf implement
 * */
int ObIStreamBuf::init()
{
  int ret = OB_SUCCESS;
  if (is_valid()) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(do_callback())) {
    last_error_code_ = ret;
  }
  return ret;
}

ObIStreamBuf::pos_type ObIStreamBuf::seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode mode)
{
  UNUSED(mode);
  pos_type ret = pos_type(off_type(-1));
  if (is_success()) {
    if (!is_valid()) {
      last_error_code_ = do_callback();
    }
    if (is_valid() && is_success()) {
      const off_type block_begin = static_cast<off_type>(stream_pos_);
      const off_type block_end = block_begin + static_cast<off_type>(egptr() - eback());
      const off_type current = block_begin + static_cast<off_type>(gptr() - eback());
      if (std::ios_base::cur == dir) {
        const off_type origin = synthetic_pos_ >= 0
                                    ? static_cast<off_type>(synthetic_pos_)
                                    : current;
        const off_type target = origin + off;
        if (synthetic_pos_ >= 0 && off == 0) {
          ret = pos_type(origin);
        } else if (synthetic_pos_ < 0 && target >= block_begin && target <= block_end) {
          setg(eback(), eback() + (target - block_begin), egptr());
          ret = pos_type(target);
        }
      } else if (std::ios_base::end == dir) {
        // IOStreamReader probes the stream length before deserializing.  The
        // callback-backed stream has no physical end pointer, so use the
        // length calculated from the LOB metadata.  Do not report a fake
        // INT64_MAX end: VSAG would then request a full block after the real
        // final partial block and turn a valid stream into VSAG 7604.
        int64_t stream_size = 0;
        if (OB_SUCC(cb_param_.get_stream_size(stream_size)) && off <= 0
            && off >= -static_cast<off_type>(stream_size)) {
          synthetic_pos_ = stream_size + static_cast<int64_t>(off);
          ret = pos_type(static_cast<off_type>(synthetic_pos_));
        }
      } else if (std::ios_base::beg == dir) {
        if (off >= block_begin && off <= block_end) {
          synthetic_pos_ = -1;
          setg(eback(), eback() + (off - block_begin), egptr());
          ret = pos_type(off);
        }
      }
    }
  }
  return ret;
}

ObIStreamBuf::pos_type ObIStreamBuf::seekpos(pos_type pos, std::ios_base::openmode mode)
{
  return seekoff(pos, std::ios_base::beg, mode);
}

std::streamsize ObIStreamBuf::xsgetn(char* s, std::streamsize n)
{
  std::streamsize get_size = 0;
  std::streamsize data_size = 0;
  if (n == 0) {
    // do nothing
  } else if (OB_ISNULL(s)) {
    last_error_code_ = OB_INVALID_ARGUMENT;
  } else if (is_success() && !is_valid()) {
    last_error_code_ = do_callback();
  }
  while (is_valid() && is_success() && get_size < n) {
    data_size = egptr() - gptr();
    std::streamsize sub_size = std::min(n - get_size, data_size);
    MEMCPY(s + get_size, gptr(), sub_size);
    gbump(static_cast<int>(sub_size));
    get_size += sub_size;
    if (get_size < n) {
      last_error_code_ = do_callback();
    }
  }
  return get_size;
}

ObIStreamBuf::int_type ObIStreamBuf::underflow()
{
  int_type ch = traits_type::eof();
  if (is_success()) {
    if (!is_valid()) {
      last_error_code_ = do_callback();
    }
    if (is_success() && is_valid()) {
      if (gptr() < egptr()) { // at least one readable char
        ch = traits_type::to_int_type(*gptr());
      } else {
        last_error_code_ = do_callback();
        if (is_success() && gptr() < egptr()) {
          ch = traits_type::to_int_type(*gptr());
        }
      }
    }
  }
  return ch;
}

int ObIStreamBuf::do_callback()
{
  int ret = OB_SUCCESS;
  char *read_data = data_;
  int64_t read_size = 0;
  if (is_valid()) {
    // do_callback() is called only after the current get area is consumed.
    // Advance the logical position before replacing the callback buffer.
    stream_pos_ += egptr() - eback();
  }
  // The input callback may return a LOB block directly instead of filling
  // data_.  The returned size is therefore the only valid readable range.
  if (OB_FAIL(cb_(read_data, capacity_, read_size, cb_param_))) {
  } else if (read_size < 0 || (read_size > 0 && OB_ISNULL(read_data))) {
    ret = OB_INVALID_DATA;
    LOG_WARN("invalid read buffer returned by callback", K(ret), K(read_data), K(read_size));
  } else {
    data_ = read_data;
    if (read_size > 0) {
      setg(data_, data_, data_ + read_size); // fill only the returned read range
    } else {
      setg(data_, data_, data_);
    }
  }
  return ret;
}
/*
 * ObVectorIndexSerializer implement
 * */
int ObVectorIndexSerializer::serialize(void *index, ObOStreamBuf::CbParam &cb_param, ObOStreamBuf::Callback &cb, const int64_t capacity)
{
  int ret = OB_SUCCESS;
  char *data = nullptr;
  if (OB_ISNULL(index) || 0 > capacity) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(data = static_cast<char*>(allocator_.alloc(capacity * sizeof(char))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ObOStreamBuf streambuf(data, capacity, cb_param, cb);
    std::ostream out(&streambuf);
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("VIndexVsagADP"));
    lib::ObLightBacktraceGuard light_backtrace_guard(false);
    if (OB_FAIL(obvectorutil::fserialize(index, out))) {
      if (streambuf.get_error_code() != OB_SUCCESS && streambuf.get_error_code() != OB_ITER_END) {
        ret = streambuf.get_error_code();
      }
    } else {
      streambuf.check_finish(); // do last callback to ensure all the data is written
      if (OB_FAIL(streambuf.get_error_code())) {
      }
    }
  }
  return ret;
}

int ObVectorIndexSerializer::deserialize(void *&index, ObIStreamBuf::CbParam &cb_param, ObIStreamBuf::Callback &cb)
{
  int ret = OB_SUCCESS;
  char *data = nullptr;
  ObIStreamBuf streambuf(nullptr, 0, cb_param, cb);
  std::istream in(&streambuf);
  int prepare_ret = cb_param.prepare_stream_size();
  if (prepare_ret != OB_SUCCESS && prepare_ret != OB_NOT_SUPPORTED) {
    ret = prepare_ret;
  } else if (OB_FAIL(streambuf.init())) {
    if (ret == OB_ITER_END) {
      LOG_INFO("[vec index deserialize] read table is empty, just return");
      ret = OB_SUCCESS;
    } else {
    }
  } else {
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("VIndexVsagADP"));
    lib::ObLightBacktraceGuard light_backtrace_guard(false);
    if (OB_FAIL(obvectorutil::fdeserialize(index, in))) {
      if (streambuf.get_error_code() != OB_SUCCESS && streambuf.get_error_code() != OB_ITER_END) {
        ret = streambuf.get_error_code();
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(streambuf.get_error_code())) {
    if (ret == OB_ITER_END) {
      LOG_INFO("[vec index deserialize] read table finish, just return");
      ret = OB_SUCCESS;
    } else {
    }
  }
  return ret;
}

int ObHNSWDeserializeCallback::CbParam::prepare_stream_size()
{
  int ret = OB_SUCCESS;
  if (stream_size_valid_) {
  } else if (OB_ISNULL(size_iter_) || OB_ISNULL(iter_) || OB_ISNULL(allocator_)
             || OB_ISNULL(lob_read_options_)) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObTableScanIterator *size_scan_iter = dynamic_cast<ObTableScanIterator *>(size_iter_);
    int64_t total_size = 0;
    if (OB_ISNULL(size_scan_iter)) {
      ret = OB_NOT_SUPPORTED;
    } else {
      int scan_ret = OB_SUCCESS;
      blocksstable::ObDatumRow *row = nullptr;
      while (OB_SUCC(ret) && OB_SUCC(scan_ret)
             && OB_SUCC(scan_ret = size_scan_iter->get_next_row(row))) {
        if (OB_ISNULL(row) || row->get_column_count() < 2) {
          scan_ret = OB_ERR_UNEXPECTED;
        } else {
          ObTextStringIter str_iter(
              ObLongTextType, CS_TYPE_BINARY, row->storage_datums_[1].get_string(), true);
          int64_t lob_size = 0;
          int tmp_ret = str_iter.init(0, lob_read_options_, allocator_);
          if (OB_FAIL(tmp_ret)) {
            scan_ret = tmp_ret;
          } else {
            tmp_ret = str_iter.get_byte_len(lob_size);
            if (OB_FAIL(tmp_ret)) {
              scan_ret = tmp_ret;
            } else if (lob_size < 0
                       || total_size > std::numeric_limits<int64_t>::max() - lob_size) {
              scan_ret = OB_SIZE_OVERFLOW;
            } else {
              total_size += lob_size;
            }
          }
        }
      }
      if (scan_ret == OB_ITER_END) {
        scan_ret = OB_SUCCESS;
      }
      if (OB_SUCC(ret) && OB_FAIL(scan_ret)) {
        ret = scan_ret;
      }
    }
    if (OB_SUCC(ret)) {
      stream_size_ = total_size;
      stream_size_valid_ = true;
    }
  }
  return ret;
}

int ObHNSWDeserializeCallback::operator()(char*& data, const int64_t data_size, int64_t &read_size, share::ObIStreamBuf::CbParam &cb_param)
{
  UNUSED(data_size);
  int ret = OB_SUCCESS;
  blocksstable::ObDatumRow *row = nullptr;
  ObDatum key_datum;
  ObDatum data_datum;
  ObHNSWDeserializeCallback::CbParam &param = static_cast<ObHNSWDeserializeCallback::CbParam&>(cb_param);
  ObTableScanIterator *row_iter = static_cast<ObTableScanIterator *>(param.iter_);
  ObIAllocator *allocator = param.allocator_;
  ObTextStringIter *&str_iter = param.str_iter_;
  ObTextStringIterState state;
  ObString src_block_data;
  if (!param.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    data = nullptr;
    read_size = 0;
    do {
      if (OB_NOT_NULL(str_iter)) {
        // try to get current next block
        state = str_iter->get_next_block(src_block_data);
        if (state == TEXTSTRING_ITER_NEXT) {
          // get next block success
          data = src_block_data.ptr();
          read_size = src_block_data.length();
        } else if (state == TEXTSTRING_ITER_END) {
          // current lob is end, need to switch to next lob
          // release current str iter
          str_iter->~ObTextStringIter();
          allocator->free(str_iter);
          str_iter = nullptr;
          allocator->reuse();
        } else {
          ret = (str_iter->get_inner_ret() != OB_SUCCESS) ? 
                str_iter->get_inner_ret() : OB_INVALID_DATA;
          // return error, release current str iter
          str_iter->~ObTextStringIter();
          allocator->free(str_iter);
          str_iter = nullptr;
          allocator->reuse();      
        }
      }
      if (OB_SUCC(ret) && OB_ISNULL(str_iter)) {
        // we should get next str_iter
        if (OB_FAIL(row_iter->get_next_row(row))) {
        } else if (OB_ISNULL(row) || row->get_column_count() < 2) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          key_datum = row->storage_datums_[0];
          data_datum = row->storage_datums_[1];
          LOG_INFO("[vec index debug] show key and data for vsag deserialize", K(key_datum), K(data_datum));
          if (OB_ISNULL(str_iter = OB_NEWx(ObTextStringIter, allocator, ObLongTextType, CS_TYPE_BINARY, data_datum.get_string(), true))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else if (OB_FAIL(str_iter->init(0, param.lob_read_options_, allocator))) {
          } else if (index_type_ == VIAT_MAX) {
            ObPluginVectorIndexAdaptor *adp = static_cast<ObPluginVectorIndexAdaptor*>(adp_);
            ObCollationType calc_cs_type = CS_TYPE_UTF8MB4_GENERAL_CI;
            uint32_t idx_ipivf = ObCharset::locate(calc_cs_type, key_datum.get_string().ptr(), key_datum.get_string().length(),
                                       "ipivf", 5, 1);
            uint32_t idx_sq = ObCharset::locate(calc_cs_type, key_datum.get_string().ptr(), key_datum.get_string().length(),
                                       "hnsw_sq", 7, 1);
            uint32_t idx_bq = ObCharset::locate(calc_cs_type, key_datum.get_string().ptr(), key_datum.get_string().length(),
                                       "hnsw_bq", 7, 1);
            uint32_t hgraph_idx = ObCharset::locate(calc_cs_type, key_datum.get_string().ptr(), key_datum.get_string().length(),
                                       "hgraph", 6, 1);
            if (OB_ISNULL(adp)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (idx_ipivf > 0) {
              index_type_ = VIAT_IPIVF;
              if (OB_FAIL(adp->try_init_snap_data(VIAT_IPIVF))) {
              }
            } else if (idx_sq > 0) {
              index_type_ = VIAT_HNSW_SQ;
              if (OB_FAIL(adp->try_init_snap_data(VIAT_HNSW_SQ))) {
              }
            } else if (idx_bq > 0) {
              index_type_ = VIAT_HNSW_BQ;
              if (OB_FAIL(adp->try_init_snap_data(VIAT_HNSW_BQ))) {
              }
            } else if (hgraph_idx > 0) {
              index_type_ = VIAT_HGRAPH;
              if (OB_FAIL(adp->try_init_snap_data(VIAT_HGRAPH))) {
              }
            } else {
              index_type_ = VIAT_HNSW;
              if (OB_FAIL(adp->try_init_snap_data(VIAT_HNSW))) {
              }
            }
            LOG_INFO("HgraphIndex vector index get key data from snap_index_table", K(ret), K(index_type_), K(key_datum.get_string()));
          }
        }
      }
    } while (OB_SUCC(ret) && OB_ISNULL(data));

    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
      ObPluginVectorIndexAdaptor *adp_ptr = static_cast<ObPluginVectorIndexAdaptor*>(adp_);
      if (OB_ISNULL(adp_ptr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (!adp_ptr->is_mem_data_init_atomic(VIRT_SNAP)) {
        if (OB_FAIL(adp_ptr->init_snap_data_without_lock(VIAT_HNSW))) {
        } else {
          ret = OB_ITER_END;
        }
      } else {
        ret = OB_ITER_END;
      }
    }
  }
  return ret;
}

int ObHNSWSerializeCallback::operator()(const char *data, const int64_t data_size, share::ObOStreamBuf::CbParam &cb_param)
{
  int ret = OB_SUCCESS;
  ObLobLocatorV2 src_lob(const_cast<char*>(data), data_size, false); // data from vsag must has no header
  ObHNSWSerializeCallback::CbParam &param = static_cast<ObHNSWSerializeCallback::CbParam&>(cb_param);
  ObVecIdxSnapshotDataWriteCtx *vctx = reinterpret_cast<ObVecIdxSnapshotDataWriteCtx*>(param.vctx_);
  ObLobManager *lob_mngr = ::oceanbase::share::server_service<::oceanbase::storage::ObLobManager>();
  ObLobAccessParam lob_param;
  lob_param.set_tmp_allocator(param.tmp_allocator_);
  lob_param.allocator_ = param.allocator_;
  lob_param.tablet_id_ = vctx->get_data_tablet_id();
  lob_param.lob_meta_tablet_id_ = vctx->get_lob_meta_tablet_id();
  lob_param.lob_piece_tablet_id_ = vctx->get_lob_piece_tablet_id();
  lob_param.inrow_threshold_ = param.lob_inrow_threshold_;
  // Data supplementation stays within the current runtime.
  lob_param.coll_type_ = CS_TYPE_BINARY;
  lob_param.offset_ = 0;
  lob_param.scan_backward_ = false;
  lob_param.is_total_quantity_log_ = true;
  lob_param.sql_mode_ = SMO_DEFAULT;
  lob_param.timeout_ = param.timeout_;
  lob_param.lob_common_ = nullptr;
  ret = lob_param.snapshot_.assign(*reinterpret_cast<transaction::ObTxReadSnapshot*>(param.snapshot_));
  if (OB_FAIL(ret)) {
  } else {
    lob_param.tx_desc_ = reinterpret_cast<transaction::ObTxDesc*>(param.tx_desc_);
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(lob_mngr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(lob_mngr->append(lob_param, src_lob))) {
  } else {
    LOG_INFO("[vec index debug] success write one data into lob tablet", K(src_lob),
              K(lob_param.lob_meta_tablet_id_), KPC(lob_param.tx_desc_));
    ObString dest_str(lob_param.handle_size_, (char*)lob_param.lob_common_);
    if (OB_FAIL(vctx->get_vals().push_back(dest_str))) {
    }
  }
  return ret;
}

};
};
