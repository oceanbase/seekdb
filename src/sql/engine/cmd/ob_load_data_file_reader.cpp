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
#include "lib/compress/zstd_1_3_8/ob_zstd_wrapper.h"
#include "lib/compress/ob_compress_util.h"
#include "sql/engine/ob_exec_context.h"

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
      LOG_WARN("failed to create ObRandomFileReader", K(ret));
    } else if (OB_FAIL(tmp_reader->open(param.filename_))) {
      LOG_WARN("fail to open random file reader", KR(ret), K(param.filename_));
      OB_DELETE(ObRandomFileReader, MEMORY_ATTR, tmp_reader);
    } else {
      file_reader = tmp_reader;
    }
  } else if (param.file_location_ == ObLoadFileLocation::CLIENT_DISK) {
    if (OB_ISNULL(param.packet_handle_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot create packet stream file reader while the packet handle is null", K(ret));
    } else {
      ObPacketStreamFileReader *tmp_reader = OB_NEW(ObPacketStreamFileReader, MEMORY_ATTR, allocator);
      if (OB_ISNULL(tmp_reader)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create ObPacketStreamFileReader", K(ret));
      } else if (OB_FAIL(tmp_reader->open(param.filename_, *param.packet_handle_, param.session_, param.timeout_ts_))) {
        LOG_WARN("failed to open packet stream file reader", KR(ret), K(param.filename_));
        OB_DELETE(ObPacketStreamFileReader, MEMORY_ATTR, tmp_reader);
      } else {
        file_reader = tmp_reader;
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported load file location", KR(ret), K(param.file_location_));
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
    LOG_WARN("ObRandomFileReader init twice", KR(ret), KP(this));
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
    LOG_WARN("ObRandomFileReader not init", KR(ret), KP(this));
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
    LOG_WARN("ObRandomFileReader not init", KR(ret), KP(this));
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
    LOG_WARN("load data won't read more data from client as the task was timeout", KR(ret), K_(timeout_ts));
  } else if (session_ != NULL && session_->is_terminate(terminate_ret)) {
    // Preserve LOAD DATA's historical SESSION_KILLED result while using the
    // common helper to distinguish QUERY_KILLED from QUERY_DEADLOCKED.
    ret = OB_ERR_SESSION_INTERRUPTED == terminate_ret ? OB_SESSION_KILLED : terminate_ret;
    LOG_WARN("load data reader terminated by session state", KR(ret));
  } else if (!eof_ && read_size == 0) {
    ret = OB_IO_ERROR;
    LOG_WARN("[should not happen] cannot read data but eof is false", KR(ret));
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

  if (OB_SUCC(ret) && OB_NOT_NULL(decompressor)) {
    if (OB_FAIL(decompressor->init())) {
      LOG_WARN("failed to init decompressor", KR(ret));
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

  if (param.compression_format_ == ObCSVGeneralFormat::ObCSVCompression::NONE) {
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

  if (OB_ISNULL(source_reader_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(buf) || capacity <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KP(buf), K(capacity));
  } else if (consumed_data_size_ >= compress_data_size_) {
    if (!source_reader_->eof()) {
      ret = read_compressed_data();
    } else {
      eof_ = true;
    }
  }

  if (OB_SUCC(ret) && compress_data_size_ > consumed_data_size_) {
    int64_t consumed_size = 0;
    ret = decompressor_->decompress(compressed_data_ + consumed_data_size_,
                                    compress_data_size_ - consumed_data_size_,
                                    consumed_size,
                                    buf,
                                    capacity,
                                    read_size);
    if (OB_FAIL(ret)) {
    } else {
      consumed_data_size_ += consumed_size;
      uncompressed_size_  += read_size;
    }
  }

  return ret;
}

int ObDecompressFileReader::read_compressed_data()
{
  int ret = OB_SUCCESS;
  char *read_buffer = compressed_data_;
  if (OB_ISNULL(source_reader_)) {
    ret = OB_NOT_INIT;
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
    ret = source_reader_->read(read_buffer, capability, read_size);
    if (OB_SUCC(ret)) {
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
  if (OB_NOT_NULL(zlib_stream_ptr_)) {
    z_streamp zstream_ptr = static_cast<z_streamp>(zlib_stream_ptr_);
    inflateEnd(zstream_ptr);
    allocator_.free(zlib_stream_ptr_);
    zlib_stream_ptr_ = nullptr;
  }
}

int ObZlibDecompressor::init()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(zlib_stream_ptr_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(zlib_stream_ptr_ = allocator_.alloc(sizeof(z_stream)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed: zlib stream object.", K(sizeof(z_stream)));
  } else {
    z_streamp zstream_ptr = static_cast<z_streamp>(zlib_stream_ptr_);
    zstream_ptr->zalloc   = ob_zlib_alloc;
    zstream_ptr->zfree    = ob_zlib_free;
    zstream_ptr->opaque   = static_cast<voidpf>(&allocator_);
    zstream_ptr->avail_in = 0;
    zstream_ptr->next_in  = Z_NULL;

    int zlib_ret = inflateInit2(zstream_ptr, 32 + MAX_WBITS);
    if (Z_OK != zlib_ret) {
      ret = OB_ERROR;
      LIB_LOG(WARN, "failed to inflateInit2", K(zlib_ret));
    }
  }
  return ret;
}

int ObZlibDecompressor::decompress(const char *src, int64_t src_size, int64_t &consumed_size,
                                   char *dest, int64_t dest_capacity, int64_t &decompressed_size)
{
  int ret = OB_SUCCESS;
  int zlib_ret = Z_OK;
  z_streamp zstream_ptr = nullptr;

  if (OB_ISNULL(zlib_stream_ptr_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(src) || src_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KP(src), K(src_size));
  } else if (OB_ISNULL(dest) || dest_capacity <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KP(dest), K(dest_capacity));
  } else if (FALSE_IT(zstream_ptr = static_cast<z_streamp>(zlib_stream_ptr_))) {
  } else if (zstream_need_reset_) {
    if (Z_OK != (zlib_ret = inflateReset(zstream_ptr))) {
      ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
      LOG_WARN("failed to reset zlib stream", K(zlib_ret));
    } else {
      zstream_need_reset_ = false;
    }
  }

  if (OB_SUCC(ret)) {
    zstream_ptr->avail_in = src_size;
    zstream_ptr->next_in  = (Bytef *)src;

    int64_t last_avail_in  = zstream_ptr->avail_in;
    int64_t last_total_out = zstream_ptr->total_out;
    zstream_ptr->next_out  = reinterpret_cast<Bytef *>(dest);
    zstream_ptr->avail_out = dest_capacity;

    zlib_ret = inflate(zstream_ptr, Z_NO_FLUSH);
    if (Z_OK == zlib_ret || Z_STREAM_END == zlib_ret) {

      consumed_size     = last_avail_in - zstream_ptr->avail_in;
      decompressed_size = zstream_ptr->total_out  - last_total_out;

      if (Z_STREAM_END == zlib_ret) {
        zstream_need_reset_ = true;
      }
    } else {
      ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
      LOG_WARN("failed to decompress", K(zlib_ret));
    }
  }

  return ret;
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
  using ObZstdWrapper = oceanbase::common::zstd_1_3_8::ObZstdWrapper;

  if (OB_NOT_NULL(zstd_stream_context_)) {
    ObZstdWrapper::free_stream_dctx(zstd_stream_context_);
    zstd_stream_context_ = nullptr;
  }
}

int ObZstdDecompressor::init()
{
  using OB_ZSTD_customMem = oceanbase::common::zstd_1_3_8::OB_ZSTD_customMem;
  using ObZstdWrapper = oceanbase::common::zstd_1_3_8::ObZstdWrapper;

  int ret = OB_SUCCESS;

  if (OB_NOT_NULL(zstd_stream_context_)) {
    ret = OB_INIT_TWICE;
  } else {
    OB_ZSTD_customMem allocator;
    allocator.customAlloc = ob_zstd_malloc;
    allocator.customFree  = ob_zstd_free;
    allocator.opaque      = &allocator_;

    ret = ObZstdWrapper::create_stream_dctx(allocator, zstd_stream_context_);
    if (OB_FAIL(ret)) {
    }
  }

  return ret;
}

int ObZstdDecompressor::decompress(const char *src, int64_t src_size, int64_t &consumed_size,
                                   char *dest, int64_t dest_capacity, int64_t &decompressed_size)
{
  using ObZstdWrapper = oceanbase::common::zstd_1_3_8::ObZstdWrapper;

  int ret = OB_SUCCESS;
  if (OB_ISNULL(zstd_stream_context_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(src) || src_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KP(src), K(src_size));
  } else if (OB_ISNULL(dest) || dest_capacity <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KP(dest), K(dest_capacity));
  } else {
    size_t tmp_consumed_size = 0;
    size_t tmp_decompressed_size = 0;
    ret = ObZstdWrapper::decompress_stream(zstd_stream_context_,
                                           src, src_size, tmp_consumed_size,
                                           dest, dest_capacity, tmp_decompressed_size);
    consumed_size = static_cast<int64_t>(tmp_consumed_size);
    decompressed_size = static_cast<int64_t>(tmp_decompressed_size);
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
