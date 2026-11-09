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

#define USING_LOG_PREFIX  SQL_ENG

#include "sql/engine/cmd/ob_load_data_file_reader.h"
#include "rpc/obmysql/ob_i_cs_mem_pool.h"
#include "rpc/obmysql/packet/ompk_local_infile.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/compress/ob_compress_util.h"
#include "rust/compression.h"
#include "sql/engine/ob_exec_context.h"
#include <algorithm>
#include <limits>

namespace oceanbase
{
namespace sql
{

const lib::ObLabel MEMORY_LABEL = lib::ObLabel("LoadDataReader");

#define MEMORY_ATTR ObMemAttr(MEMORY_LABEL)

/**
 * ObFileReadParam
 */

ObFileReadParam::ObFileReadParam()
    : compression_format_(ObCSVGeneralFormat::ObCSVCompression::NONE),
      packet_handle_(NULL),
      session_(NULL),
      timeout_ts_(-1)
{
}

int ObFileReadParam::parse_compression_format(ObString compression_name,
                                              ObString filename,
                                              ObCSVGeneralFormat::ObCSVCompression &compression_format)
{
  int ret = OB_SUCCESS;
  if (compression_name.length() == 0) {
    compression_format = ObCSVGeneralFormat::ObCSVCompression::NONE;
  } else if (OB_FAIL(compression_algorithm_from_string(compression_name, compression_format))) {
  } else if (ObCSVGeneralFormat::ObCSVCompression::AUTO == compression_format) {
    ret = compression_algorithm_from_suffix(filename, compression_format);
  }
  return ret;
}

/**
 * ObFileReader
 */

int ObFileReader::open(const ObFileReadParam &param, ObIAllocator &allocator, ObFileReader *& file_reader)
{
  int ret = OB_SUCCESS;
  file_reader = nullptr;
  
  if (param.file_location_ == ObLoadFileLocation::SERVER_DISK) {
    ObRandomFileReader *tmp_reader = OB_NEW(ObRandomFileReader, MEMORY_ATTR, allocator);
    if (OB_ISNULL(tmp_reader)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(tmp_reader->open(param.filename_))) {
      OB_DELETE(ObRandomFileReader, MEMORY_ATTR, tmp_reader);
    } else {
      file_reader = tmp_reader;
    }
  } else if (param.file_location_ == ObLoadFileLocation::CLIENT_DISK) {
    if (OB_ISNULL(param.packet_handle_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObPacketStreamFileReader *tmp_reader = OB_NEW(ObPacketStreamFileReader, MEMORY_ATTR, allocator);
      if (OB_ISNULL(tmp_reader)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(tmp_reader->open(param.filename_, *param.packet_handle_, param.session_, param.timeout_ts_))) {
        OB_DELETE(ObPacketStreamFileReader, MEMORY_ATTR, tmp_reader);
      } else {
        file_reader = tmp_reader;
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
  }

  if (OB_SUCC(ret)) {
    ObFileReader *decompress_reader = nullptr;
    ret = open_decompress_reader(param, allocator, file_reader, decompress_reader);
    if (OB_SUCC(ret) && OB_NOT_NULL(decompress_reader)) {
      file_reader = decompress_reader;
    }
  }

  return ret;
}

void ObFileReader::destroy(ObFileReader *file_reader)
{
  if (OB_NOT_NULL(file_reader)) {
    OB_DELETE(ObFileReader, MEMORY_ATTR, file_reader);
  }
}

int ObFileReader::open_decompress_reader(const ObFileReadParam &param,
                                         ObIAllocator &allocator,
                                         ObFileReader *source_reader,
                                         ObFileReader *&file_reader)
{
  int ret = OB_SUCCESS;
  if (param.compression_format_ == ObCSVGeneralFormat::ObCSVCompression::NONE) {
    file_reader = source_reader;
  } else {
    ObDecompressFileReader *tmp_reader = OB_NEW(ObDecompressFileReader, MEMORY_ATTR, allocator);
    if (OB_ISNULL(tmp_reader)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(tmp_reader->open(param, source_reader))) {
      LOG_WARN("failed to open decompress file reader");
      OB_DELETE(ObDecompressFileReader, MEMORY_ATTR, tmp_reader);
    } else {
      file_reader = tmp_reader;
    }
  }
  return ret;
}

int ObFileReader::readn(char *buffer, int64_t count, int64_t &read_size)
{
  int ret = OB_SUCCESS;
  read_size = 0;
  while (OB_SUCC(ret) && !eof() && read_size < count) {
    int64_t this_read_size = 0;
    ret = this->read(buffer + read_size, count - read_size, this_read_size);
    if (OB_SUCC(ret)) {
      read_size += this_read_size;
    }
  }
  return ret;
}

/**
 * ObRandomFileReader
 */

ObRandomFileReader::ObRandomFileReader(ObIAllocator &allocator)
    : ObFileReader(allocator),
      offset_(0),
      eof_(false),
      is_inited_(false)
{
}

ObRandomFileReader::~ObRandomFileReader()
{
}

int ObRandomFileReader::open(const ObString &filename)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(file_reader_.open(filename.ptr(), false))) {
  } else {
    filename_ = filename;
    offset_ = 0;
    eof_ = false;
    is_inited_ = true;
  }
  return ret;
}

int ObRandomFileReader::read(char *buf, int64_t count, int64_t &read_size)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(file_reader_.pread(buf, count, offset_, read_size))) {
  } else if (0 == read_size) {
    eof_ = true;
  } else {
    offset_ += read_size;
  }
  return ret;
}

int ObRandomFileReader::seek(int64_t offset)
{
  offset_ = offset;
  return OB_SUCCESS;
}

int ObRandomFileReader::get_file_size(int64_t &file_size)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    file_size = ::get_file_size(filename_.ptr());
  }
  return ret;
}

/**
 * ObPacketStreamFileReader
 */
class CSMemPoolAdaptor : public obmysql::ObICSMemPool
{
public:
  explicit CSMemPoolAdaptor(ObIAllocator *allocator)
      : allocator_(allocator)
  {}

  virtual ~CSMemPoolAdaptor() {}

  void *alloc(int64_t size) override
  {
    return allocator_->alloc(size);
  }

private:
  ObIAllocator *allocator_;
};

ObPacketStreamFileReader::ObPacketStreamFileReader(ObIAllocator &allocator)
    : ObStreamFileReader(allocator),
      packet_handle_(NULL),
      session_(NULL),
      timeout_ts_(INT64_MAX),
      // OB_MALLOC_MIDDLE_BLOCK_SIZE 64K. one mysql packet is about 16K
      arena_allocator_(allocator, OB_MALLOC_MIDDLE_BLOCK_SIZE),
      cached_packet_(NULL),
      received_size_(0),
      read_size_(0),
      eof_(false)
{
}

ObPacketStreamFileReader::~ObPacketStreamFileReader()
{
  int ret = OB_SUCCESS;

  LOG_INFO("load data local try to receive all packets from client if eof is false", K_(eof));

  // We read all data from client before close the file.
  // We will stop to handle the process while something error.
  // But the client must send all file content to us and the
  // normal SQL processor cannot handle the packets, so we
  // eat all packets with file content.
  
  // We will wait at most 10 seconds if there is no more data come in.
  const int64_t wait_timeout = 10 * 1000000L; // seconds
  timeout_ts_ = ObTimeUtility::current_time() + wait_timeout;
  int64_t last_received_size = received_size_;
  while (!eof_ && OB_SUCC(ret) && !is_killed()
         && ObTimeUtility::current_time() <= timeout_ts_) {
    ret = receive_packet();
    if (received_size_ > last_received_size) {
      last_received_size = received_size_;
      timeout_ts_ = ObTimeUtility::current_time() + wait_timeout;
    }
  }
  LOG_INFO("load data local file reader exit", K(ret), K(eof_), K(timeout_ts_), K(ObTimeUtility::current_time()));
  if (!eof_ && OB_NOT_NULL(session_) && OB_NOT_NULL(session_->get_cur_exec_ctx())) {
    session_->get_cur_exec_ctx()->set_need_disconnect(true);
    LOG_WARN("we'll close the connection as we can't read all of the file content", K(eof_));
  }
  const int release_ret = release_packet();
  if (OB_SUCCESS != release_ret) {
  }
  arena_allocator_.reset();
}

int ObPacketStreamFileReader::open(const ObString &filename,
                                   ObIClientPacketChannel &packet_handle,
                                   ObSQLSessionInfo *session,
                                   int64_t timeout_ts)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(packet_handle_)) {
    ret = OB_INIT_TWICE;
  } else {

    // in `load data local` request, we should send the filename to client
    obmysql::OMPKLocalInfile filename_packet;
    filename_packet.set_filename(filename);
    if (OB_FAIL(packet_handle.response_packet(filename_packet))) {
    } else if (OB_FAIL(packet_handle.flush_buffer(false /*is_last*/))) {
    } else {
      LOG_INFO("[load data local]send filename to client success", K(filename));
    }

    packet_handle_ = &packet_handle;
    session_       = session;
    timeout_ts_    = timeout_ts;
    received_size_ = 0;
    read_size_     = 0;
    eof_           = false;
    LOG_INFO("[load data local] open socket file reader", K_(timeout_ts));
  }
  return ret;
}

/**
 * As decripted in MySQL/MariaDB document, client send the file content with
 * continous packets and `eof` with an empty packet. Every non-empty packet
 * has the format:
 * -------------------
 * MySQL Packet Header
 * string<EOF>
 * -------------------
 * The notation is "string<EOF>" Strings whose length will be calculated by
 * the packet remaining length.
 */
int ObPacketStreamFileReader::read(char *buf, int64_t count, int64_t &read_size)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cached_packet_) || read_size_ == received_size_) {
    ret = receive_packet();
  }

  const int64_t remain_in_packet = received_size_ - read_size_;
  if (OB_SUCC(ret) && OB_NOT_NULL(cached_packet_) && (!eof_ || remain_in_packet > 0)) {
    read_size = MIN(count, remain_in_packet);
    // LOCAL_INFILE_DATA packets carry no command byte: get_cdata() points at
    // the file-content payload itself and get_clen() is its true length.
    const int64_t packet_offset = cached_packet_->get_clen() - remain_in_packet;
    MEMCPY(buf, cached_packet_->get_cdata() + packet_offset, read_size);
    read_size_ += read_size;
  } else {
    read_size = 0;
  }

  int terminate_ret = OB_SUCCESS;
  if (is_timeout()) {
    ret = OB_TIMEOUT;
  } else if (session_ != NULL && session_->is_terminate(terminate_ret)) {
    // Preserve LOAD DATA's historical SESSION_KILLED result while using the
    // common helper to distinguish QUERY_KILLED from QUERY_DEADLOCKED.
    ret = OB_ERR_SESSION_INTERRUPTED == terminate_ret ? OB_SESSION_KILLED : terminate_ret;
  } else if (!eof_ && read_size == 0) {
    ret = OB_IO_ERROR;
  }
  return ret;
}

int ObPacketStreamFileReader::receive_packet()
{
  int ret = OB_SUCCESS;
  ret = release_packet();

  if (OB_SUCC(ret)) {
    arena_allocator_.reuse();
    CSMemPoolAdaptor mem_pool(&arena_allocator_);

    if (!is_timeout() && !is_killed()) {
      obmysql::ObMySQLPacket *pkt = NULL;
      const int64_t timeout_us = -1 == timeout_ts_
          ? -1
          : MAX(0, timeout_ts_ - ObTimeUtility::current_time());
      ret = packet_handle_->wait_packet(mem_pool, timeout_us, pkt);
      cached_packet_ = static_cast<obmysql::ObMySQLRawPacket *>(pkt);
    }

    if (OB_SUCC(ret) && OB_NOT_NULL(cached_packet_)) {
      const int pkt_len = cached_packet_->get_clen();
      if (0 == pkt_len) { // empty packet
        eof_ = true;
        (void)release_packet();
      } else {
        received_size_ += pkt_len;
      }
    }
  }

  // If anything wrong, we end the reading
  if (OB_FAIL(ret)) {
    eof_ = true;
  }
  return ret;
}

int ObPacketStreamFileReader::release_packet()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(cached_packet_)) {
    ret = packet_handle_->release_packet(cached_packet_);
    cached_packet_ = NULL;
  }
  return ret;
}

bool ObPacketStreamFileReader::is_timeout() const
{
  return timeout_ts_ != -1 && ObTimeUtility::current_time() >= timeout_ts_;
}

bool ObPacketStreamFileReader::is_killed() const
{
  return NULL != session_ && (session_->is_query_killed() || session_->is_zombie());
}

/**
 * ObDecompressor
 */
ObDecompressor::ObDecompressor(ObIAllocator &allocator)
    : allocator_(allocator)
{}

ObDecompressor::~ObDecompressor()
{
}

int ObDecompressor::create(ObCSVGeneralFormat::ObCSVCompression format,
                           ObIAllocator &allocator,
                           ObDecompressor *&decompressor)
{
  int ret = OB_SUCCESS;

  decompressor = nullptr;

  switch (format) {
    case ObCSVGeneralFormat::ObCSVCompression::NONE: {
      ret = OB_INVALID_ARGUMENT;
    } break;

    case ObCSVGeneralFormat::ObCSVCompression::GZIP:
    case ObCSVGeneralFormat::ObCSVCompression::DEFLATE: {
      decompressor = OB_NEW(ObZlibDecompressor, MEMORY_ATTR, allocator, format);
    } break;

    case ObCSVGeneralFormat::ObCSVCompression::ZSTD: {
      decompressor = OB_NEW(ObZstdDecompressor, MEMORY_ATTR, allocator);
    } break;

    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unsupported compression format", K(format));
    } break;
  }

  if (OB_SUCC(ret) && OB_ISNULL(decompressor)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_SUCC(ret)) {
    if (OB_FAIL(decompressor->init())) {
      ObDecompressor::destroy(decompressor);
      decompressor = nullptr;
    }
  }

  return ret;
}

void ObDecompressor::destroy(ObDecompressor *decompressor)
{
  if (OB_NOT_NULL(decompressor)) {
    decompressor->destroy();
    OB_DELETE(ObDecompressor, MEMORY_ATTR, decompressor);
  }
}

/**
 * ObDecompressFileReader
 */
const int64_t ObDecompressFileReader::COMPRESSED_DATA_BUFFER_SIZE = 2 * 1024 * 1024;

ObDecompressFileReader::ObDecompressFileReader(ObIAllocator &allocator)
    : ObStreamFileReader(allocator)
{}

ObDecompressFileReader::~ObDecompressFileReader()
{
  if (OB_NOT_NULL(source_reader_)) {
    OB_DELETE(ObFileReader, MEMORY_ATTR, source_reader_);
  }

  if (OB_NOT_NULL(decompressor_)) {
    ObDecompressor::destroy(decompressor_);
  }

  if (OB_NOT_NULL(compressed_data_)) {
    allocator_.free(compressed_data_);
    compressed_data_ = nullptr;
  }
}

int ObDecompressFileReader::open(const ObFileReadParam &param, ObFileReader *source_reader)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(source_reader)
      || param.compression_format_ == ObCSVGeneralFormat::ObCSVCompression::NONE) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObDecompressor::create(param.compression_format_, allocator_, decompressor_))) {
  } else if (OB_ISNULL(compressed_data_ = (char *)allocator_.alloc(COMPRESSED_DATA_BUFFER_SIZE))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate buffer.", K(COMPRESSED_DATA_BUFFER_SIZE));
  } else if (FALSE_IT(source_reader_ = source_reader)) {
  }

  return ret;
}

int ObDecompressFileReader::read(char *buf, int64_t capacity, int64_t &read_size)
{
  int ret = OB_SUCCESS;

  read_size = 0;

  if (OB_ISNULL(source_reader_) || OB_ISNULL(decompressor_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(buf) || capacity <= 0) {
    ret = OB_INVALID_ARGUMENT;
  }

  while (OB_SUCC(ret) && read_size == 0 && !eof_) {
    const int64_t available = compress_data_size_ - consumed_data_size_;
    const char *input = available > 0 ? compressed_data_ + consumed_data_size_ : nullptr;
    int64_t consumed_size = 0;
    if (OB_FAIL(decompressor_->decompress(input, available, consumed_size, buf, capacity, read_size))) {
    } else if (consumed_size < 0 || consumed_size > available
               || read_size < 0 || read_size > capacity) {
      ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
    } else if (read_size > std::numeric_limits<int64_t>::max() - uncompressed_size_) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      consumed_data_size_ += consumed_size;
      uncompressed_size_ += read_size;
      if (read_size > 0 || consumed_size > 0) {
      } else if (source_reader_->eof()) {
        if (available != 0) {
          ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
        } else if (OB_FAIL(decompressor_->finish())) {
        } else {
          eof_ = true;
        }
      } else if (available == COMPRESSED_DATA_BUFFER_SIZE) {
        ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
      } else if (OB_FAIL(read_compressed_data())) {
      } else if (compress_data_size_ == available && !source_reader_->eof()) {
        // A source may temporarily return no bytes without reaching EOF.
        break;
      }
    }
  }

  return ret;
}

int ObDecompressFileReader::read_compressed_data()
{
  int ret = OB_SUCCESS;
  char *read_buffer = compressed_data_;
  if (OB_ISNULL(source_reader_) || OB_ISNULL(compressed_data_)) {
    ret = OB_NOT_INIT;
  } else if (consumed_data_size_ < 0 || consumed_data_size_ > compress_data_size_
             || compress_data_size_ > COMPRESSED_DATA_BUFFER_SIZE) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(consumed_data_size_ < compress_data_size_)) {
    // backup data
    const int64_t last_data_size = compress_data_size_ - consumed_data_size_;
    MEMMOVE(compressed_data_, compressed_data_ + consumed_data_size_, last_data_size);
    read_buffer = compressed_data_ + last_data_size;
    consumed_data_size_ = 0;
    compress_data_size_ = last_data_size;
  } else if (consumed_data_size_ == compress_data_size_) {
    consumed_data_size_ = 0;
    compress_data_size_ = 0;
  }

  if (OB_SUCC(ret)) {
    // read data from source reader
    int64_t read_size = 0;
    int64_t capability = COMPRESSED_DATA_BUFFER_SIZE - compress_data_size_;
    if (capability <= 0) {
      ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
    } else if (OB_FAIL(source_reader_->read(read_buffer, capability, read_size))) {
    } else if (read_size < 0 || read_size > capability) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      compress_data_size_ += read_size;
    }
  }
  return ret;
}

/**
 * ObZlibDecompressor
 */

ObZlibDecompressor::ObZlibDecompressor(ObIAllocator &allocator,
                                       ObCSVGeneralFormat::ObCSVCompression compression_format)
    : ObDecompressor(allocator), compression_format_(compression_format)
{}

ObZlibDecompressor::~ObZlibDecompressor()
{
  this->destroy();
}

void ObZlibDecompressor::destroy()
{
  decoder_.reset();
  stream_finished_ = false;
}

int ObZlibDecompressor::init()
{
  int ret = OB_SUCCESS;
  if (decoder_.has_value()) {
    ret = OB_INIT_TWICE;
  } else {
    decoder_.emplace(compression::new_stream_decoder());
    stream_finished_ = false;
  }
  return ret;
}

int ObZlibDecompressor::decompress(const char *src, int64_t src_size, int64_t &consumed_size,
                                   char *dest, int64_t dest_capacity, int64_t &decompressed_size)
{
  int ret = OB_SUCCESS;
  consumed_size = 0;
  decompressed_size = 0;

  if (!decoder_.has_value()) {
    ret = OB_NOT_INIT;
  } else if (src_size < 0 || (src_size > 0 && OB_ISNULL(src))
             || static_cast<uint64_t>(src_size) > std::numeric_limits<ptrdiff_t>::max()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(dest) || dest_capacity <= 0
             || static_cast<uint64_t>(dest_capacity) > std::numeric_limits<ptrdiff_t>::max()) {
    ret = OB_INVALID_ARGUMENT;
  }

  if (OB_SUCC(ret)) {
    const auto result = compression::decoder_process(
        *decoder_.value(),
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(src_size > 0 ? src : ""), src_size),
        rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(dest), dest_capacity));
    if (compression::Code::Ok == result.code) {
      consumed_size = result.consumed;
      decompressed_size = result.produced;
      stream_finished_ = result.stream_end;
    } else {
      ret = compression::Code::OutOfMemory == result.code
          ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_COMPRESS_DECOMPRESS_DATA;
      LOG_WARN("failed to decompress");
    }
  }

  return ret;
}

int ObZlibDecompressor::finish()
{
  return !decoder_.has_value() ? OB_NOT_INIT
      : stream_finished_ ? OB_SUCCESS : OB_ERR_COMPRESS_DECOMPRESS_DATA;
}

/**
 * ObZstdDecompressor
 */

ObZstdDecompressor::ObZstdDecompressor(ObIAllocator &allocator)
    : ObDecompressor(allocator)
{}

ObZstdDecompressor::~ObZstdDecompressor()
{
  this->destroy();
}

void ObZstdDecompressor::destroy()
{
  decoder_.reset();
}

int ObZstdDecompressor::init()
{
  int ret = OB_SUCCESS;
  if (decoder_.has_value()) {
    ret = OB_INIT_TWICE;
  } else {
    decoder_.emplace(compression::new_zstd_decoder());
  }

  return ret;
}

int ObZstdDecompressor::decompress(const char *src, int64_t src_size, int64_t &consumed_size,
                                   char *dest, int64_t dest_capacity, int64_t &decompressed_size)
{
  int ret = OB_SUCCESS;
  consumed_size = 0;
  decompressed_size = 0;
  if (!decoder_.has_value()) {
    ret = OB_NOT_INIT;
  } else if (src_size < 0 || (src_size > 0 && OB_ISNULL(src))
             || static_cast<uint64_t>(src_size) > std::numeric_limits<ptrdiff_t>::max()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(dest) || dest_capacity <= 0
             || static_cast<uint64_t>(dest_capacity) > std::numeric_limits<ptrdiff_t>::max()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const auto *input = reinterpret_cast<const uint8_t *>(src_size > 0 ? src : "");
    const auto result = compression::zstd_decode(**decoder_,
        rust::Slice<const uint8_t>(input, static_cast<size_t>(src_size)),
        reinterpret_cast<uint8_t *>(dest), static_cast<size_t>(dest_capacity));
    if (OB_SUCC(ret = common::compression_error_code(result.code))) {
      consumed_size = static_cast<int64_t>(result.consumed);
      decompressed_size = static_cast<int64_t>(result.produced);
    }
  }
  return ret;
}

int ObZstdDecompressor::finish()
{
  return decoder_.has_value()
      ? common::compression_error_code(compression::zstd_decoder_finish(**decoder_)) : OB_NOT_INIT;
}

} // namespace sql
} // namespace oceanbase
