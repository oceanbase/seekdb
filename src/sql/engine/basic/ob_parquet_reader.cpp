/**
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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_parquet_reader.h"

#include "lib/charset/ob_charset.h"
#include "lib/oblog/ob_log.h"
#include "lib/file/file_directory_utils.h"
#include <stdio.h>


namespace oceanbase
{
using namespace common;
namespace sql
{

// ---------------------------------------------------------------------------
// Snappy block-format decompression (self-contained)
// ---------------------------------------------------------------------------
static int snappy_uncompress(const char *in, int64_t in_len, char *out, int64_t out_cap,
                             int64_t &out_len)
{
  int ret = OB_SUCCESS;
  out_len = 0;
  int64_t pos = 0;
  // read varint uncompressed length
  int64_t expected_len = 0;
  int shift = 0;
  while (pos < in_len) {
    const uint8_t b = static_cast<uint8_t>(in[pos++]);
    expected_len |= static_cast<int64_t>(b & 0x7F) << shift;
    if (0 == (b & 0x80)) {
      break;
    }
    shift += 7;
    if (shift > 63) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("snappy varint too long", K(ret));
      return ret;
    }
  }
  if (expected_len > out_cap) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("snappy output too large", K(ret), K(expected_len), K(out_cap));
    return ret;
  }
  while (pos < in_len) {
    const uint8_t tag = static_cast<uint8_t>(in[pos++]);
    const uint8_t tag_type = tag & 0x03;
    if (0x00 == tag_type) {
      // literal
      int64_t length = (tag >> 2) + 1;
      if (length > 60) {
        const int64_t extra_bytes = length - 60;
        if (pos + extra_bytes > in_len) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("snappy literal length truncated", K(ret));
          return ret;
        }
        int64_t val = 0;
        for (int64_t i = 0; i < extra_bytes; ++i) {
          val |= static_cast<int64_t>(static_cast<uint8_t>(in[pos + i])) << (8 * i);
        }
        pos += extra_bytes;
        length = val + 1;
      }
      if (pos + length > in_len || out_len + length > out_cap) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("snappy literal overflow", K(ret));
        return ret;
      }
      MEMCPY(out + out_len, in + pos, length);
      out_len += length;
      pos += length;
    } else {
      int64_t copy_len = 0;
      int64_t offset = 0;
      if (0x01 == tag_type) {
        // copy with 1-byte offset
        copy_len = 4 + ((tag >> 2) & 0x07);
        offset = (static_cast<int64_t>(tag & 0xE0) << 3)
            | static_cast<uint8_t>(in[pos++]);
      } else if (0x02 == tag_type) {
        // copy with 2-byte offset
        copy_len = (tag >> 2) + 1;
        offset = static_cast<uint8_t>(in[pos])
            | (static_cast<int64_t>(static_cast<uint8_t>(in[pos + 1])) << 8);
        pos += 2;
      } else {
        // copy with 4-byte offset
        copy_len = (tag >> 2) + 1;
        offset = static_cast<uint8_t>(in[pos])
            | (static_cast<int64_t>(static_cast<uint8_t>(in[pos + 1])) << 8)
            | (static_cast<int64_t>(static_cast<uint8_t>(in[pos + 2])) << 16)
            | (static_cast<int64_t>(static_cast<uint8_t>(in[pos + 3])) << 24);
        pos += 4;
      }
      if (offset <= 0 || offset > out_len || out_len + copy_len > out_cap) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("snappy copy overflow", K(ret), K(offset), K(copy_len), K(out_len));
        return ret;
      }
      // copy byte by byte to handle overlapping ranges
      int64_t src = out_len - offset;
      for (int64_t i = 0; i < copy_len; ++i) {
        out[out_len++] = out[src++];
      }
    }
  }
  if (out_len != expected_len) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("snappy length mismatch", K(ret), K(out_len), K(expected_len));
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Thrift compact protocol reader (subset needed for parquet metadata)
// ---------------------------------------------------------------------------
// Compact type ids: 1 BOOLEAN_TRUE, 2 BOOLEAN_FALSE, 3 BYTE, 4 I16, 5 I32,
// 6 I64, 7 DOUBLE, 8 BINARY, 9 LIST, 10 SET, 11 MAP, 12 STRUCT.
static inline bool is_int_type(const uint8_t t) { return 4 == t || 5 == t || 6 == t; }

class ThriftCompactReader
{
public:
  ThriftCompactReader(const char *buf, int64_t len)
    : buf_(buf), len_(len), pos_(0), last_field_id_stack_(), last_field_id_(0)
  {
    last_field_id_stack_.push_back(0);
  }
  bool eof() const { return pos_ >= len_; }
  int64_t get_pos() const { return pos_; }
  void set_pos(int64_t pos) { pos_ = pos; }

  // returns (field_id, type); type 0 means STOP
  int read_field_begin(int16_t &field_id, uint8_t &type)
  {
    int ret = OB_SUCCESS;
    uint8_t byte = 0;
    if (OB_FAIL(read_byte(byte))) {
    } else if (0 == byte) {
      type = 0;
      field_id = 0;
      last_field_id_ = last_field_id_stack_.at(last_field_id_stack_.count() - 1);
    } else {
      type = byte & 0x0F;
      const int16_t delta = (byte >> 4) & 0x0F;
      if (0 == delta) {
        int64_t id = 0;
        if (OB_FAIL(read_zigzag(id))) {
        } else {
          // long form carries the absolute field id (not a delta)
          field_id = static_cast<int16_t>(id);
        }
      } else {
        field_id = static_cast<int16_t>(last_field_id_ + delta);
      }
      if (OB_SUCC(ret)) {
        last_field_id_ = field_id;
      }
    }
    return ret;
  }

  // Thrift restores the enclosing struct's last-field-id when a nested struct
  // ends, so the current id (not 0) has to be saved on push.
  void push_field_scope()
  {
    last_field_id_stack_.push_back(last_field_id_);
    last_field_id_ = 0;
  }
  void pop_field_scope()
  {
    if (last_field_id_stack_.empty()) {
      last_field_id_ = 0;
    } else {
      last_field_id_ = last_field_id_stack_.at(last_field_id_stack_.count() - 1);
      last_field_id_stack_.pop_back();
    }
  }

  int read_byte(uint8_t &val)
  {
    if (pos_ + 1 > len_) {
      return OB_ERR_UNEXPECTED;
    }
    val = static_cast<uint8_t>(buf_[pos_++]);
    return OB_SUCCESS;
  }

  int read_zigzag(int64_t &val)
  {
    int ret = OB_SUCCESS;
    uint64_t zigzag = 0;
    int shift = 0;
    while (true) {
      if (pos_ >= len_) {
        return OB_ERR_UNEXPECTED;
      }
      const uint8_t b = static_cast<uint8_t>(buf_[pos_++]);
      zigzag |= static_cast<uint64_t>(b & 0x7F) << shift;
      if (0 == (b & 0x80)) {
        break;
      }
      shift += 7;
      if (shift > 63) {
        return OB_ERR_UNEXPECTED;
      }
    }
    val = static_cast<int64_t>((zigzag >> 1) ^ (~(zigzag & 1) + 1));
    return ret;
  }

  int read_i32(int32_t &val)
  {
    int64_t v = 0;
    int ret = read_zigzag(v);
    val = static_cast<int32_t>(v);
    return ret;
  }

  int read_i64(int64_t &val) { return read_zigzag(val); }

  // Unsigned LEB128 varint: used by string/list/map sizes in the compact
  // protocol (they are NOT zigzag encoded, unlike i16/i32/i64 fields).
  int read_varint(uint64_t &val)
  {
    int ret = OB_SUCCESS;
    val = 0;
    int shift = 0;
    while (true) {
      if (pos_ >= len_) {
        ret = OB_ERR_UNEXPECTED;
        break;
      }
      const uint8_t b = static_cast<uint8_t>(buf_[pos_++]);
      val |= static_cast<uint64_t>(b & 0x7F) << shift;
      if (0 == (b & 0x80)) {
        break;
      }
      shift += 7;
      if (shift > 63) {
        ret = OB_ERR_UNEXPECTED;
        break;
      }
    }
    return ret;
  }

  int read_double(double &val)
  {
    if (pos_ + 8 > len_) {
      return OB_ERR_UNEXPECTED;
    }
    // thrift compact writes doubles in little-endian (since thrift 0.9.1+ big-endian?
    // parquet writers use little-endian per spec)
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
      bits |= static_cast<uint64_t>(static_cast<uint8_t>(buf_[pos_ + i])) << (8 * i);
    }
    pos_ += 8;
    MEMCPY(&val, &bits, 8);
    return OB_SUCCESS;
  }

  int read_binary(ObString &val)
  {
    int ret = OB_SUCCESS;
    uint64_t length = 0;
    if (OB_FAIL(read_varint(length))) {
    } else if (length > static_cast<uint64_t>(len_ - pos_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      val.assign_ptr(buf_ + pos_, static_cast<int32_t>(length));
      pos_ += static_cast<int64_t>(length);
    }
    return ret;
  }

  // for list/set: returns element type and count
  int read_list_begin(uint8_t &elem_type, int64_t &count)
  {
    int ret = OB_SUCCESS;
    uint8_t byte = 0;
    if (OB_FAIL(read_byte(byte))) {
    } else {
      elem_type = byte & 0x0F;
      int64_t size = (byte >> 4) & 0x0F;
      if (0x0F == size) {
        uint64_t big = 0;
        if (OB_FAIL(read_varint(big))) {
        } else {
          size = static_cast<int64_t>(big);
        }
      }
      count = size;
    }
    return ret;
  }

  // skip one value of the given type
  int skip(uint8_t type)
  {
    int ret = OB_SUCCESS;
    switch (type) {
      case 1:    // BOOLEAN_TRUE: value lives in the field header
      case 2: {  // BOOLEAN_FALSE
        break;
      }
      case 3: {  // byte
        uint8_t b = 0;
        ret = read_byte(b);
        break;
      }
      case 4:
      case 5:
      case 6: {  // i16 / i32 / i64 are all zigzag varints
        int64_t v = 0;
        ret = read_zigzag(v);
        break;
      }
      case 7: {  // double
        double d = 0;
        ret = read_double(d);
        break;
      }
      case 8: {
        ObString s;
        ret = read_binary(s);
        break;
      }
      case 9:
      case 10: {
        uint8_t et = 0;
        int64_t cnt = 0;
        if (OB_FAIL(read_list_begin(et, cnt))) {
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
            ret = skip(et);
          }
        }
        break;
      }
      case 11: {  // map
        uint8_t kt = 0, vt = 0;
        uint64_t cnt = 0;
        uint8_t b = 0;
        if (OB_FAIL(read_byte(b))) {
        } else if (0 == b) {
          cnt = 0;
        } else {
          kt = b & 0x0F;
          vt = (b >> 4) & 0x0F;
          if (OB_FAIL(read_varint(cnt))) {
          } else {
            for (uint64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
              if (OB_FAIL(skip(kt))) {
              } else if (OB_FAIL(skip(vt))) {
              }
            }
          }
        }
        break;
      }
      case 12: {  // struct
        push_field_scope();
        while (OB_SUCC(ret)) {
          int16_t fid = 0;
          uint8_t ft = 0;
          if (OB_FAIL(read_field_begin(fid, ft))) {
          } else if (0 == ft) {
            break;
          } else if (OB_FAIL(skip(ft))) {
          }
        }
        pop_field_scope();
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unknown thrift compact type", K(ret), K(type));
        break;
    }
    return ret;
  }

private:
  const char *buf_;
  int64_t len_;
  int64_t pos_;
  ObSEArray<int16_t, 8> last_field_id_stack_;
  int16_t last_field_id_;
};

// ---------------------------------------------------------------------------
// Parquet metadata structures
// ---------------------------------------------------------------------------
enum ParquetPhysicalType
{
  PQ_BOOLEAN = 0,
  PQ_INT32 = 1,
  PQ_INT64 = 2,
  PQ_INT96 = 3,
  PQ_FLOAT = 4,
  PQ_DOUBLE = 5,
  PQ_BYTE_ARRAY = 6,
  PQ_FIXED_LEN_BYTE_ARRAY = 7,
  PQ_UNKNOWN = 99
};

enum ParquetCodec
{
  PQ_CODEC_UNCOMPRESSED = 0,
  PQ_CODEC_SNAPPY = 1,
  PQ_CODEC_GZIP = 2,
  PQ_CODEC_LZO = 3,
  PQ_CODEC_BROTLI = 4,
  PQ_CODEC_LZ4 = 5,
  PQ_CODEC_ZSTD = 6
};

enum ParquetEncoding
{
  PQ_ENC_PLAIN = 0,
  PQ_ENC_PLAIN_DICTIONARY = 2,
  PQ_ENC_RLE = 3,
  PQ_ENC_DELTA_BINARY_PACKED = 4,
  PQ_ENC_DELTA_LENGTH_BYTE_ARRAY = 5,
  PQ_ENC_DELTA_BYTE_ARRAY = 6,
  PQ_ENC_RLE_DICTIONARY = 8
};

struct ParquetColumnMeta
{
  ParquetColumnMeta()
    : physical_type_(PQ_UNKNOWN), codec_(PQ_CODEC_UNCOMPRESSED), num_values_(0),
      data_page_offset_(0), dict_page_offset_(-1), total_compressed_size_(0),
      max_def_level_(0), type_length_(0)
  {}
  ParquetPhysicalType physical_type_;
  ParquetCodec codec_;
  int64_t num_values_;
  int64_t data_page_offset_;
  int64_t dict_page_offset_;
  int64_t total_compressed_size_;
  int64_t max_def_level_;
  int32_t type_length_;
  ObString name_;
};

struct ParquetRowGroupMeta
{
  int64_t num_rows_;
  int64_t columns_offset_;  // index into column metas
  int64_t column_cnt_;
  ParquetRowGroupMeta() : num_rows_(0), columns_offset_(0), column_cnt_(0) {}
};

class ParquetFileMeta
{
public:
  ParquetFileMeta() : num_rows_(0), version_(0) {}
  ObArray<ParquetColumnMeta> columns_;      // schema-level columns (name/type/def level)
  ObArray<ParquetColumnMeta> chunk_metas_;  // flat per (row group, column) chunk info
  ObArray<ParquetRowGroupMeta> row_groups_;
  int64_t num_rows_;
  int32_t version_;
};

// ---------------------------------------------------------------------------
// Parquet file: reading + metadata parsing
// ---------------------------------------------------------------------------
class ObParquetFile
{
public:
  ObParquetFile(ObIAllocator &alloc) : alloc_(alloc), file_(), file_size_(0), meta_() {}
  ~ObParquetFile() { file_.close(); }

  int open(const ObString &path);
  // footer must stay valid while file is open: parse into own arena
  int parse_footer();
  const ParquetFileMeta &get_meta() const { return meta_; }
  // read a chunk of the file into buf
  int read_range(int64_t offset, int64_t length, char *buf);
  int64_t get_file_size() const { return file_size_; }
private:
  ObIAllocator &alloc_;
  common::ObFileReader file_;
  int64_t file_size_;
  ParquetFileMeta meta_;
};

int ObParquetFile::open(const ObString &path)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(file_.open(path, false))) {
    LOG_USER_ERROR(OB_FILE_NOT_EXIST, static_cast<int32_t>(path.length()), path.ptr());
    LOG_WARN("open parquet file failed", K(ret), K(path));
  } else if (OB_FAIL(FileDirectoryUtils::get_file_size(path.ptr(), file_size_))) {
    LOG_WARN("get file size failed", K(ret));
  } else if (file_size_ < 12) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("file too small to be parquet", K(ret), K(file_size_));
  }
  return ret;
}

int ObParquetFile::read_range(int64_t offset, int64_t length, char *buf)
{
  int ret = OB_SUCCESS;
  int64_t read_size = 0;
  if (OB_FAIL(file_.pread(buf, length, offset, read_size))) {
    LOG_WARN("pread failed", K(ret));
  } else if (read_size != length) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("short read", K(ret), K(read_size), K(length));
  }
  return ret;
}

int ObParquetFile::parse_footer()
{
  int ret = OB_SUCCESS;
  // the 8 trailing bytes are: 4 byte metadata length, then the PAR1 magic
  char tail[8];
  if (OB_FAIL(read_range(file_size_ - 8, 8, tail))) {
    LOG_WARN("read footer tail failed", K(ret));
  } else if (0 != MEMCMP(tail + 4, "PAR1", 4)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not a parquet file", K(ret));
  } else {
    const uint32_t meta_len = static_cast<uint8_t>(tail[0])
        | (static_cast<uint32_t>(static_cast<uint8_t>(tail[1])) << 8)
        | (static_cast<uint32_t>(static_cast<uint8_t>(tail[2])) << 16)
        | (static_cast<uint32_t>(static_cast<uint8_t>(tail[3])) << 24);
    if (meta_len + 12 > file_size_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("bad footer length", K(ret), K(meta_len), K(file_size_));
    } else {
      char *meta_buf = static_cast<char *>(alloc_.alloc(meta_len));
      if (OB_ISNULL(meta_buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc meta buffer failed", K(ret));
      } else if (OB_FAIL(read_range(file_size_ - 8 - meta_len, meta_len, meta_buf))) {
        LOG_WARN("read footer failed", K(ret));
      } else {
        ThriftCompactReader rd(meta_buf, meta_len);
        // FileMetaData fields:
        // 1: version i32, 2: schema list<SchemaElement>, 3: num_rows i64,
        // 4: row_groups list<RowGroup>, 5: kv meta, 6: created_by
        // Schema: first element is root, children follow
        struct SchemaElem
        {
          ParquetPhysicalType type_;
          int32_t type_length_;
          int32_t repetition_type_;
          ObString name_;
          int32_t num_children_;
          SchemaElem()
            : type_(PQ_UNKNOWN), type_length_(0), repetition_type_(0),
              name_(), num_children_(0)
          {}
        };
        ObArray<SchemaElem> schema;
        ObArray<ParquetRowGroupMeta> row_groups;
        while (OB_SUCC(ret)) {
          int16_t fid = 0;
          uint8_t ftype = 0;
          if (OB_FAIL(rd.read_field_begin(fid, ftype))) {
            LOG_WARN("read footer field failed", K(ret));
          } else if (0 == ftype) {
            break;
          } else if (1 == fid && is_int_type(ftype)) {
            if (OB_FAIL(rd.read_i32(meta_.version_))) {
              LOG_WARN("read version failed", K(ret));
            }
          } else if (3 == fid && is_int_type(ftype)) {
            if (OB_FAIL(rd.read_i64(meta_.num_rows_))) {
              LOG_WARN("read num_rows failed", K(ret));
            }
          } else if (2 == fid && 9 == ftype) {
            uint8_t et = 0;
            int64_t cnt = 0;
            if (OB_FAIL(rd.read_list_begin(et, cnt))) {
              LOG_WARN("read schema list failed", K(ret));
            } else {
              for (int64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
                SchemaElem elem;
                rd.push_field_scope();
                while (OB_SUCC(ret)) {
                  int16_t sfid = 0;
                  uint8_t sftype = 0;
                  if (OB_FAIL(rd.read_field_begin(sfid, sftype))) {
                  } else if (0 == sftype) {
                    break;
                  } else if (1 == sfid && is_int_type(sftype)) {
                    int32_t t = 0;
                    if (OB_FAIL(rd.read_i32(t))) {
                    } else {
                      elem.type_ = static_cast<ParquetPhysicalType>(t);
                    }
                  } else if (2 == sfid && is_int_type(sftype)) {
                    if (OB_FAIL(rd.read_i32(elem.type_length_))) {
                    }
                  } else if (3 == sfid && is_int_type(sftype)) {
                    if (OB_FAIL(rd.read_i32(elem.repetition_type_))) {
                    }
                  } else if (4 == sfid && 8 == sftype) {
                    if (OB_FAIL(rd.read_binary(elem.name_))) {
                    }
                  } else if (5 == sfid && is_int_type(sftype)) {
                    if (OB_FAIL(rd.read_i32(elem.num_children_))) {
                    }
                  } else if (OB_FAIL(rd.skip(sftype))) {
                  }
                }
                rd.pop_field_scope();
                if (OB_SUCC(ret) && OB_FAIL(schema.push_back(elem))) {
                  LOG_WARN("push schema elem failed", K(ret));
                }
              }
            }
          } else if (4 == fid && 9 == ftype) {
            uint8_t et = 0;
            int64_t cnt = 0;
            if (OB_FAIL(rd.read_list_begin(et, cnt))) {
              LOG_WARN("read row groups failed", K(ret));
            } else {
              for (int64_t g = 0; OB_SUCC(ret) && g < cnt; ++g) {
                ParquetRowGroupMeta rg;
                rg.columns_offset_ = meta_.chunk_metas_.count();
                rd.push_field_scope();
                while (OB_SUCC(ret)) {
                  int16_t gfid = 0;
                  uint8_t gftype = 0;
                  if (OB_FAIL(rd.read_field_begin(gfid, gftype))) {
                  } else if (0 == gftype) {
                    break;
                  } else if (3 == gfid && is_int_type(gftype)) {
                    if (OB_FAIL(rd.read_i64(rg.num_rows_))) {
                    }
                  } else if (1 == gfid && 9 == gftype) {
                    uint8_t cet = 0;
                    int64_t ccnt = 0;
                    if (OB_FAIL(rd.read_list_begin(cet, ccnt))) {
                      LOG_WARN("read columns list failed", K(ret));
                    } else {
                      for (int64_t c = 0; OB_SUCC(ret) && c < ccnt; ++c) {
                        ParquetColumnMeta cm;
                        rd.push_field_scope();
                        while (OB_SUCC(ret)) {
                          int16_t cfid = 0;
                          uint8_t cftype = 0;
                          if (OB_FAIL(rd.read_field_begin(cfid, cftype))) {
                          } else if (0 == cftype) {
                            break;
                          } else if (3 == cfid && 12 == cftype) {
                            // ColumnMetaData
                            rd.push_field_scope();
                            while (OB_SUCC(ret)) {
                              int16_t mfid = 0;
                              uint8_t mftype = 0;
                              if (OB_FAIL(rd.read_field_begin(mfid, mftype))) {
                              } else if (0 == mftype) {
                                break;
                              } else if (1 == mfid && is_int_type(mftype)) {
                                int32_t t = 0;
                                if (OB_FAIL(rd.read_i32(t))) {
                                } else {
                                  cm.physical_type_ = static_cast<ParquetPhysicalType>(t);
                                }
                              } else if (4 == mfid && is_int_type(mftype)) {
                                int32_t codec = 0;
                                if (OB_FAIL(rd.read_i32(codec))) {
                                } else {
                                  cm.codec_ = static_cast<ParquetCodec>(codec);
                                }
                              } else if (5 == mfid && is_int_type(mftype)) {
                                if (OB_FAIL(rd.read_i64(cm.num_values_))) {
                                }
                              } else if (7 == mfid && is_int_type(mftype)) {
                                if (OB_FAIL(rd.read_i64(cm.total_compressed_size_))) {
                                }
                              } else if (9 == mfid && is_int_type(mftype)) {
                                if (OB_FAIL(rd.read_i64(cm.data_page_offset_))) {
                                }
                              } else if (11 == mfid && is_int_type(mftype)) {
                                if (OB_FAIL(rd.read_i64(cm.dict_page_offset_))) {
                                }
                              } else if (3 == mfid && 9 == mftype) {
                                // path_in_schema: list<string>, keep only leaf name
                                uint8_t pet = 0;
                                int64_t pcnt = 0;
                                if (OB_FAIL(rd.read_list_begin(pet, pcnt))) {
                                } else {
                                  for (int64_t k = 0; OB_SUCC(ret) && k < pcnt; ++k) {
                                    ObString part;
                                    if (OB_FAIL(rd.read_binary(part))) {
                                    } else {
                                      cm.name_ = part;
                                    }
                                  }
                                }
                              } else if (OB_FAIL(rd.skip(mftype))) {
                              }
                            }
                            rd.pop_field_scope();
                          } else if (OB_FAIL(rd.skip(cftype))) {
                          }
                        }
                        rd.pop_field_scope();
                        if (OB_SUCC(ret) && OB_FAIL(meta_.chunk_metas_.push_back(cm))) {
                          LOG_WARN("push chunk meta failed", K(ret));
                        }
                      }
                    }
                  } else if (OB_FAIL(rd.skip(gftype))) {
                  }
                }
                rd.pop_field_scope();
                rg.column_cnt_ = meta_.chunk_metas_.count() - rg.columns_offset_;
                if (OB_SUCC(ret) && OB_FAIL(row_groups.push_back(rg))) {
                  LOG_WARN("push row group failed", K(ret));
                }
              }
            }
          } else if (OB_FAIL(rd.skip(ftype))) {
          }
        }
        if (OB_SUCC(ret)) {
          // build column metas from schema (skip root, use leaf names) merged
          // with per-row-group column metas
          if (schema.count() < 2) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("empty parquet schema", K(ret), K(schema.count()));
          } else {
            // compute max def levels: OPTIONAL -> 1 (non-nested only)
            meta_.columns_.reset();
            for (int64_t i = 1; OB_SUCC(ret) && i < schema.count(); ++i) {
              const SchemaElem &se = schema.at(i);
              ParquetColumnMeta cm;
              cm.name_ = se.name_;
              cm.physical_type_ = se.type_;
              cm.type_length_ = se.type_length_;
              cm.max_def_level_ = (1 == se.repetition_type_) ? 1 : 0;
              if (2 == se.repetition_type_) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "repeated (list) parquet columns");
                LOG_WARN("repeated schema element not supported", K(ret), K(se.name_));
              } else if (OB_FAIL(meta_.columns_.push_back(cm))) {
                LOG_WARN("push column failed", K(ret));
              }
            }
            if (OB_SUCC(ret)) {
              meta_.row_groups_ = row_groups;
              // patch per-row-group physical info (codec/offsets) onto column
              // metas is done per row group by the reader; store nothing here
            }
          }
        }
      }
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// RLE / bit-packed hybrid decoder (levels and dictionary indices)
// ---------------------------------------------------------------------------
class RleDecoder
{
public:
  RleDecoder(const char *buf, int64_t len, uint8_t bit_width)
    : buf_(buf), len_(len), pos_(0), bit_width_(bit_width),
      run_count_(0), run_value_(0), literal_cnt_(0), literal_pos_(0)
  {}
  // decode up to n values; returns number decoded
  int64_t decode(int64_t *out, int64_t n)
  {
    int64_t decoded = 0;
    while (decoded < n) {
      if (run_count_ > 0) {
        while (run_count_ > 0 && decoded < n) {
          out[decoded++] = run_value_;
          --run_count_;
        }
      } else if (literal_cnt_ > 0) {
        while (literal_cnt_ > 0 && decoded < n) {
          out[decoded++] = read_bits(bit_width_);
          --literal_cnt_;
        }
        // skip padding to byte boundary handled by bit position
      } else {
        if (pos_ >= len_) {
          break;
        }
        // read run header (uleb128)
        uint64_t header = 0;
        int shift = 0;
        while (pos_ < len_) {
          const uint8_t b = static_cast<uint8_t>(buf_[pos_++]);
          header |= static_cast<uint64_t>(b & 0x7F) << shift;
          if (0 == (b & 0x80)) {
            break;
          }
          shift += 7;
        }
        bit_pos_ = 0;
        if (header & 1) {
          literal_cnt_ = static_cast<int64_t>((header >> 1) * 8);
          literal_pos_ = 0;
        } else {
          run_count_ = static_cast<int64_t>(header >> 1);
          run_value_ = 0;
          const int64_t byte_width = (bit_width_ + 7) / 8;
          for (int64_t i = 0; i < byte_width && pos_ < len_; ++i) {
            run_value_ |= static_cast<int64_t>(static_cast<uint8_t>(buf_[pos_++])) << (8 * i);
          }
        }
      }
    }
    return decoded;
  }
private:
  int64_t read_bits(uint8_t width)
  {
    int64_t val = 0;
    for (uint8_t i = 0; i < width; ++i) {
      const int64_t byte_idx = pos_ + ((bit_pos_ + i) >> 3);
      const int64_t bit_idx = (bit_pos_ + i) & 7;
      if (byte_idx >= len_) {
        return val;
      }
      val |= static_cast<int64_t>(((static_cast<uint8_t>(buf_[byte_idx]) >> bit_idx) & 1)) << i;
    }
    bit_pos_ += width;
    if (bit_pos_ >= 8) {
      pos_ += bit_pos_ >> 3;
      bit_pos_ &= 7;
    }
    return val;
  }
  const char *buf_;
  int64_t len_;
  int64_t pos_;
  uint8_t bit_width_;
  int64_t run_count_;
  int64_t run_value_;
  int64_t literal_cnt_;
  int64_t literal_pos_;
  int64_t bit_pos_;
};

// ---------------------------------------------------------------------------
// Page-level column reader
// ---------------------------------------------------------------------------
class ObParquetColumnReader
{
public:
  ObParquetColumnReader(ObIAllocator &alloc, ObParquetFile &file,
                        const ParquetColumnMeta &meta)
    : alloc_(alloc), file_(file), meta_(meta), values_()
  {}
  // decode all values of one row group for this column into values_
  int decode_row_group(const ParquetRowGroupMeta &rg,
                       const ParquetColumnMeta &rg_col_meta);
  const ObArray<ObObj> &get_values() const { return values_; }
  void reset_values() { values_.reset(); }
private:
  int decode_page_v1(const char *page, int64_t page_len, int64_t num_values,
                     ParquetEncoding enc, ObIArray<ObObj> &out);
  int decode_page_v2(const char *page, int64_t page_len, int64_t num_values,
                     int64_t num_nulls, ParquetEncoding enc,
                     int64_t def_len, int64_t rep_len, bool is_compressed,
                     ObIArray<ObObj> &out);
  int decode_plain_values(const char *buf, int64_t len, int64_t num_values,
                          ObIArray<ObObj> &out);
  int decode_dict_values(const char *buf, int64_t len, int64_t num_values,
                         ObIArray<ObObj> &out);
  int build_dictionary(const char *page, int64_t page_len, int64_t num_values);
  int make_obj_from_physical(const char *&buf, const char *end, bool &valid, ObObj &out);
  int make_obj_from_bits(int64_t bits, ObObj &out);

  ObIAllocator &alloc_;
  ObParquetFile &file_;
  ParquetColumnMeta meta_;  // schema-level info (type/name/def level)
  ObArray<ObObj> values_;
  ObArray<ObObj> dictionary_;
  ObArenaAllocator page_arena_;
};

int ObParquetColumnReader::build_dictionary(const char *page, int64_t page_len,
                                            int64_t num_values)
{
  int ret = OB_SUCCESS;
  dictionary_.reset();
  ret = decode_plain_values(page, page_len, num_values, dictionary_);
  return ret;
}

int ObParquetColumnReader::make_obj_from_bits(int64_t bits, ObObj &out)
{
  int ret = OB_SUCCESS;
  out.reset();
  switch (meta_.physical_type_) {
    case PQ_BOOLEAN:
      out.set_tinyint(bits ? 1 : 0);
      break;
    case PQ_INT32:
      out.set_int(static_cast<int32_t>(bits));
      break;
    case PQ_INT64:
      out.set_int(static_cast<int64_t>(bits));
      break;
    case PQ_FLOAT:
      out.set_double(static_cast<double>(*reinterpret_cast<const float *>(&bits)));
      break;
    case PQ_DOUBLE:
      out.set_double(*reinterpret_cast<const double *>(&bits));
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected type for bit value", K(ret), K(meta_.physical_type_));
      break;
  }
  return ret;
}

int ObParquetColumnReader::make_obj_from_physical(const char *&buf, const char *end,
                                                  bool &valid, ObObj &out)
{
  int ret = OB_SUCCESS;
  valid = true;
  out.reset();
  switch (meta_.physical_type_) {
    case PQ_BOOLEAN: {
      // caller handles bit packing separately (see decode_plain_values)
      ret = OB_ERR_UNEXPECTED;
      break;
    }
    case PQ_INT32: {
      if (buf + 4 > end) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        int32_t v = 0;
        MEMCPY(&v, buf, 4);
        buf += 4;
        out.set_int(v);
      }
      break;
    }
    case PQ_INT64: {
      if (buf + 8 > end) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        int64_t v = 0;
        MEMCPY(&v, buf, 8);
        buf += 8;
        out.set_int(v);
      }
      break;
    }
    case PQ_FLOAT: {
      if (buf + 4 > end) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        float v = 0;
        MEMCPY(&v, buf, 4);
        buf += 4;
        out.set_double(v);
      }
      break;
    }
    case PQ_DOUBLE: {
      if (buf + 8 > end) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        double v = 0;
        MEMCPY(&v, buf, 8);
        buf += 8;
        out.set_double(v);
      }
      break;
    }
    case PQ_BYTE_ARRAY: {
      if (buf + 4 > end) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        uint32_t len = 0;
        MEMCPY(&len, buf, 4);
        buf += 4;
        if (buf + len > end) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          // alloc(0) returns NULL: an empty BYTE_ARRAY value must not be
          // allocated, otherwise it is reported as an out-of-memory error.
          char *copy = NULL;
          if (len > 0) {
            copy = static_cast<char *>(alloc_.alloc(len));
            if (OB_ISNULL(copy)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
            } else {
              MEMCPY(copy, buf, len);
            }
          }
          if (OB_SUCC(ret)) {
            out.set_varchar(ObString(static_cast<int32_t>(len), copy));
            out.set_collation_type(common::ObCharset::get_default_collation(
                common::ObCharset::get_default_charset()));
            buf += len;
          }
        }
      }
      break;
    }
    default:
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "this parquet physical type");
      break;
  }
  return ret;
}

int ObParquetColumnReader::decode_plain_values(const char *buf, int64_t len,
                                               int64_t num_values, ObIArray<ObObj> &out)
{
  int ret = OB_SUCCESS;
  const char *p = buf;
  const char *end = buf + len;
  if (PQ_BOOLEAN == meta_.physical_type_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < num_values; ++i) {
      const int64_t byte_idx = i >> 3;
      const int64_t bit_idx = i & 7;
      if (byte_idx >= len) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("boolean page truncated", K(ret));
      } else {
        const bool bit = 0 != ((static_cast<uint8_t>(p[byte_idx]) >> bit_idx) & 1);
        ObObj obj;
        obj.set_tinyint(bit ? 1 : 0);
        if (OB_FAIL(out.push_back(obj))) {
          LOG_WARN("push value failed", K(ret));
        }
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < num_values; ++i) {
      bool valid = false;
      ObObj obj;
      if (OB_FAIL(make_obj_from_physical(p, end, valid, obj))) {
        LOG_WARN("decode plain value failed", K(ret));
      } else if (OB_FAIL(out.push_back(obj))) {
        LOG_WARN("push value failed", K(ret));
      }
    }
  }
  return ret;
}

int ObParquetColumnReader::decode_dict_values(const char *buf, int64_t len,
                                              int64_t num_values, ObIArray<ObObj> &out)
{
  int ret = OB_SUCCESS;
  if (len < 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dict data page too short", K(ret));
  } else {
    const uint8_t bit_width = static_cast<uint8_t>(buf[0]);
    RleDecoder rle(buf + 1, len - 1, bit_width);
    for (int64_t i = 0; OB_SUCC(ret) && i < num_values; ++i) {
      int64_t idx = 0;
      if (rle.decode(&idx, 1) != 1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("rle dictionary index underflow", K(ret));
      } else if (idx < 0 || idx >= dictionary_.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("dictionary index out of range", K(ret), K(idx), K(dictionary_.count()));
      } else if (OB_FAIL(out.push_back(dictionary_.at(idx)))) {
        LOG_WARN("push value failed", K(ret));
      }
    }
  }
  return ret;
}

int ObParquetColumnReader::decode_page_v1(const char *page, int64_t page_len,
                                          int64_t num_values, ParquetEncoding enc,
                                          ObIArray<ObObj> &out)
{
  int ret = OB_SUCCESS;
  const char *p = page;
  const char *end = page + page_len;
  int64_t non_null_cnt = num_values;
  // definition levels (v1): 4-byte LE length + RLE data (when max def > 0)
  ObArray<int64_t> def_levels;
  if (meta_.max_def_level_ > 0) {
    if (p + 4 > end) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("def level length truncated", K(ret));
    } else {
      uint32_t dl_len = 0;
      MEMCPY(&dl_len, p, 4);
      p += 4;
      if (p + dl_len > end) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("def level data truncated", K(ret));
      } else {
        // bit width = ceil(log2(max_def+1))
        uint8_t bw = 0;
        int64_t tmp = meta_.max_def_level_ + 1;
        while (tmp > 1) {
          ++bw;
          tmp >>= 1;
        }
        RleDecoder rle(p, dl_len, bw);
        def_levels.reserve(num_values);
        int64_t total = 0;
        while (total < num_values) {
          int64_t buf[256];
          const int64_t got = rle.decode(buf, 256);
          if (got <= 0) {
            break;
          }
          for (int64_t i = 0; OB_SUCC(ret) && i < got && total < num_values; ++i) {
            if (OB_FAIL(def_levels.push_back(buf[i]))) {
              LOG_WARN("push def level failed", K(ret));
            }
            ++total;
          }
        }
        p += dl_len;
        if (OB_SUCC(ret) && def_levels.count() != num_values) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("def level count mismatch", K(ret), K(def_levels.count()), K(num_values));
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (PQ_ENC_PLAIN == enc) {
    if (meta_.max_def_level_ > 0) {
      // count non-nulls first, then decode values in order
      int64_t nn = 0;
      for (int64_t i = 0; i < def_levels.count(); ++i) {
        if (def_levels.at(i) >= meta_.max_def_level_) {
          ++nn;
        }
      }
      non_null_cnt = nn;
      ObArray<ObObj> raw;
      if (OB_FAIL(decode_plain_values(p, end - p, non_null_cnt, raw))) {
        LOG_WARN("decode raw values failed", K(ret));
      } else {
        int64_t vi = 0;
        for (int64_t i = 0; OB_SUCC(ret) && i < num_values; ++i) {
          ObObj obj;
          if (def_levels.at(i) < meta_.max_def_level_) {
            obj.set_null();
          } else {
            obj = raw.at(vi++);
          }
          if (OB_FAIL(out.push_back(obj))) {
            LOG_WARN("push value failed", K(ret));
          }
        }
      }
    } else if (OB_FAIL(decode_plain_values(p, end - p, num_values, out))) {
      LOG_WARN("decode plain values failed", K(ret));
    }
  } else if (PQ_ENC_PLAIN_DICTIONARY == enc || PQ_ENC_RLE_DICTIONARY == enc) {
    if (OB_FAIL(decode_dict_values(p, end - p, num_values, out))) {
      LOG_WARN("decode dict values failed", K(ret));
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "this parquet value encoding");
    LOG_WARN("unsupported encoding", K(ret), K(enc));
  }
  return ret;
}

int ObParquetColumnReader::decode_page_v2(const char *page, int64_t page_len,
                                          int64_t num_values, int64_t num_nulls,
                                          ParquetEncoding enc, int64_t def_len,
                                          int64_t rep_len, bool is_compressed,
                                          ObIArray<ObObj> &out)
{
  int ret = OB_SUCCESS;
  const char *p = page;
  const char *end = page + page_len;
  // v2: rep levels (uncompressed), def levels (uncompressed), values
  p += rep_len;
  if (p + def_len > end) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("v2 def levels truncated", K(ret));
  } else {
    ObArray<int64_t> def_levels;
    if (meta_.max_def_level_ > 0) {
      uint8_t bw = 0;
      int64_t tmp = meta_.max_def_level_ + 1;
      while (tmp > 1) {
        ++bw;
        tmp >>= 1;
      }
      RleDecoder rle(p, def_len, bw);
      int64_t total = 0;
      while (total < num_values) {
        int64_t buf[256];
        const int64_t got = rle.decode(buf, 256);
        if (got <= 0) {
          break;
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < got && total < num_values; ++i) {
          if (OB_FAIL(def_levels.push_back(buf[i]))) {
            LOG_WARN("push def level failed", K(ret));
          }
          ++total;
        }
      }
      if (OB_SUCC(ret) && def_levels.count() != num_values) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("v2 def level count mismatch", K(ret), K(def_levels.count()), K(num_values));
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      const char *vp = p + def_len;
      const int64_t vlen = end - vp;
      if (is_compressed) {
        // values part is compressed; def/rep levels are not
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("v2 compressed pages handled via page-level decompression", K(ret));
      } else if (PQ_ENC_PLAIN == enc) {
        if (meta_.max_def_level_ > 0) {
          int64_t nn = num_values - num_nulls;
          ObArray<ObObj> raw;
          if (OB_FAIL(decode_plain_values(vp, vlen, nn, raw))) {
            LOG_WARN("decode raw values failed", K(ret));
          } else {
            int64_t vi = 0;
            for (int64_t i = 0; OB_SUCC(ret) && i < num_values; ++i) {
              ObObj obj;
              if (def_levels.at(i) < meta_.max_def_level_) {
                obj.set_null();
              } else {
                obj = raw.at(vi++);
              }
              if (OB_FAIL(out.push_back(obj))) {
                LOG_WARN("push value failed", K(ret));
              }
            }
          }
        } else if (OB_FAIL(decode_plain_values(vp, vlen, num_values, out))) {
          LOG_WARN("decode plain values failed", K(ret));
        }
      } else if (PQ_ENC_PLAIN_DICTIONARY == enc || PQ_ENC_RLE_DICTIONARY == enc) {
        if (OB_FAIL(decode_dict_values(vp, vlen, num_values, out))) {
          LOG_WARN("decode dict values failed", K(ret));
        }
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "this parquet value encoding");
      }
    }
  }
  return ret;
}

int ObParquetColumnReader::decode_row_group(const ParquetRowGroupMeta &rg,
                                            const ParquetColumnMeta &rg_col_meta)
{
  int ret = OB_SUCCESS;
  values_.reset();
  dictionary_.reset();
  // determine the scan range: from dict page offset (if any) or data page
  // offset, up to the total compressed size
  int64_t start = rg_col_meta.data_page_offset_;
  if (rg_col_meta.dict_page_offset_ > 0 && rg_col_meta.dict_page_offset_ < start) {
    start = rg_col_meta.dict_page_offset_;
  }
  const int64_t end = start + rg_col_meta.total_compressed_size_;
  int64_t pos = start;
  int64_t decoded = 0;
  // compressed scratch buffer
  ObArenaAllocator scratch("ParquetPage");
  while (OB_SUCC(ret) && decoded < rg_col_meta.num_values_ && pos < end) {
    // read page header (at most 64KB header)
    const int64_t hdr_max = std::min<int64_t>(64 * 1024, end - pos);
    char *hdr_buf = static_cast<char *>(scratch.alloc(hdr_max));
    if (OB_ISNULL(hdr_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc page header failed", K(ret));
    } else if (OB_FAIL(file_.read_range(pos, hdr_max, hdr_buf))) {
      LOG_WARN("read page header failed", K(ret));
    } else {
      ThriftCompactReader rd(hdr_buf, hdr_max);
      int32_t page_type = -1;
      int32_t uncomp_size = 0;
      int32_t comp_size = 0;
      int32_t dp_num_values = 0;
      int32_t dp_encoding = 0;
      int32_t dp_def_enc = 0;
      int32_t dp_rep_enc = 0;
      int32_t dict_num_values = 0;
      int32_t dict_encoding = 0;
      int32_t v2_num_values = 0;
      int32_t v2_num_nulls = 0;
      int32_t v2_def_len = 0;
      int32_t v2_rep_len = 0;
      bool v2_compressed = true;
      int32_t v2_encoding = 0;
      rd.push_field_scope();
      while (OB_SUCC(ret)) {
        int16_t fid = 0;
        uint8_t ftype = 0;
        if (OB_FAIL(rd.read_field_begin(fid, ftype))) {
        } else if (0 == ftype) {
          break;
        } else if (1 == fid && is_int_type(ftype)) {
          if (OB_FAIL(rd.read_i32(page_type))) {
          }
        } else if (2 == fid && is_int_type(ftype)) {
          if (OB_FAIL(rd.read_i32(uncomp_size))) {
          }
        } else if (3 == fid && is_int_type(ftype)) {
          if (OB_FAIL(rd.read_i32(comp_size))) {
          }
        } else if (5 == fid && 12 == ftype) {
          // DataPageHeader
          rd.push_field_scope();
          while (OB_SUCC(ret)) {
            int16_t dfid = 0;
            uint8_t dftype = 0;
            if (OB_FAIL(rd.read_field_begin(dfid, dftype))) {
            } else if (0 == dftype) {
              break;
            } else if (1 == dfid && is_int_type(dftype)) {
              if (OB_FAIL(rd.read_i32(dp_num_values))) {
              }
            } else if (2 == dfid && is_int_type(dftype)) {
              if (OB_FAIL(rd.read_i32(dp_encoding))) {
              }
            } else if (3 == dfid && is_int_type(dftype)) {
              if (OB_FAIL(rd.read_i32(dp_def_enc))) {
              }
            } else if (4 == dfid && is_int_type(dftype)) {
              if (OB_FAIL(rd.read_i32(dp_rep_enc))) {
              }
            } else if (OB_FAIL(rd.skip(dftype))) {
            }
          }
          rd.pop_field_scope();
        } else if (7 == fid && 12 == ftype) {
          // DictionaryPageHeader
          rd.push_field_scope();
          while (OB_SUCC(ret)) {
            int16_t dfid = 0;
            uint8_t dftype = 0;
            if (OB_FAIL(rd.read_field_begin(dfid, dftype))) {
            } else if (0 == dftype) {
              break;
            } else if (1 == dfid && is_int_type(dftype)) {
              if (OB_FAIL(rd.read_i32(dict_num_values))) {
              }
            } else if (2 == dfid && is_int_type(dftype)) {
              if (OB_FAIL(rd.read_i32(dict_encoding))) {
              }
            } else if (OB_FAIL(rd.skip(dftype))) {
            }
          }
          rd.pop_field_scope();
        } else if (8 == fid && 12 == ftype) {
          // DataPageHeaderV2
          rd.push_field_scope();
          while (OB_SUCC(ret)) {
            int16_t dfid = 0;
            uint8_t dftype = 0;
            if (OB_FAIL(rd.read_field_begin(dfid, dftype))) {
            } else if (0 == dftype) {
              break;
            } else if (1 == dfid && is_int_type(dftype)) {
              int32_t t = 0;
              if (OB_FAIL(rd.read_i32(t))) {
              } else {
                v2_num_values = t;
              }
            } else if (2 == dfid && is_int_type(dftype)) {
              int32_t t = 0;
              if (OB_FAIL(rd.read_i32(t))) {
              } else {
                v2_num_nulls = t;
              }
            } else if (4 == dfid && is_int_type(dftype)) {
              int32_t t = 0;
              if (OB_FAIL(rd.read_i32(t))) {
              } else {
                v2_encoding = t;
              }
            } else if (5 == dfid && is_int_type(dftype)) {
              int32_t t = 0;
              if (OB_FAIL(rd.read_i32(t))) {
              } else {
                v2_def_len = t;
              }
            } else if (6 == dfid && is_int_type(dftype)) {
              int32_t t = 0;
              if (OB_FAIL(rd.read_i32(t))) {
              } else {
                v2_rep_len = t;
              }
            } else if (7 == dfid && (1 == dftype || 2 == dftype)) {
              // thrift compact booleans carry the value in the field header
              v2_compressed = (1 == dftype);
            } else if (OB_FAIL(rd.skip(dftype))) {
            }
          }
          rd.pop_field_scope();
        } else if (OB_FAIL(rd.skip(ftype))) {
        }
      }
      rd.pop_field_scope();
      if (OB_FAIL(ret)) {
        LOG_WARN("parse page header failed", K(ret));
      } else {
        const int64_t hdr_size = rd.get_pos();
        if (comp_size <= 0 || pos + hdr_size + comp_size > end + 1024) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("bad page size", K(ret), K(comp_size), K(pos), K(hdr_size));
        } else {
          char *comp_buf = static_cast<char *>(scratch.alloc(comp_size));
          const int64_t data_cap = std::max(uncomp_size, comp_size);
          char *data_buf = static_cast<char *>(scratch.alloc(data_cap > 0 ? data_cap : comp_size));
          if (OB_ISNULL(comp_buf) || OB_ISNULL(data_buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("alloc page buffers failed", K(ret));
          } else if (OB_FAIL(file_.read_range(pos + hdr_size, comp_size, comp_buf))) {
            LOG_WARN("read page data failed", K(ret));
          } else {
            int64_t data_len = uncomp_size;
            if (3 == page_type) {
              // DATA_PAGE_V2 keeps rep/def levels uncompressed; when the page is
              // marked compressed only the values region is a compressed block,
              // which the v2 decoder below handles. Never decompress a v2 page
              // as a whole here.
              MEMCPY(data_buf, comp_buf, comp_size);
              data_len = comp_size;
            } else if (PQ_CODEC_UNCOMPRESSED == rg_col_meta.codec_) {
              MEMCPY(data_buf, comp_buf, comp_size);
            } else if (PQ_CODEC_SNAPPY == rg_col_meta.codec_) {
              if (OB_FAIL(snappy_uncompress(comp_buf, comp_size, data_buf, uncomp_size, data_len))) {
                LOG_WARN("snappy uncompress failed", K(ret));
              }
            } else {
              ret = OB_NOT_SUPPORTED;
              LOG_USER_ERROR(OB_NOT_SUPPORTED, "this parquet compression codec");
              LOG_WARN("unsupported codec", K(ret), K(rg_col_meta.codec_));
            }
            if (OB_SUCC(ret)) {
              if (0 == page_type) {
                // DATA_PAGE v1
                ObArray<ObObj> page_values;
                if (OB_FAIL(decode_page_v1(data_buf, data_len, dp_num_values,
                                           static_cast<ParquetEncoding>(dp_encoding),
                                           page_values))) {
                  LOG_WARN("decode page v1 failed", K(ret));
                } else {
                  for (int64_t i = 0; OB_SUCC(ret) && i < page_values.count(); ++i) {
                    if (OB_FAIL(values_.push_back(page_values.at(i)))) {
                      LOG_WARN("push value failed", K(ret));
                    }
                  }
                  decoded += dp_num_values;
                }
              } else if (2 == page_type) {
                // DICTIONARY_PAGE
                if (OB_FAIL(build_dictionary(data_buf, data_len, dict_num_values))) {
                  LOG_WARN("build dictionary failed", K(ret));
                }
              } else if (3 == page_type) {
                // DATA_PAGE_V2
                ObArray<ObObj> page_values;
                if (PQ_CODEC_UNCOMPRESSED == rg_col_meta.codec_ || !v2_compressed) {
                  if (OB_FAIL(decode_page_v2(data_buf, data_len, v2_num_values, v2_num_nulls,
                                             static_cast<ParquetEncoding>(v2_encoding),
                                             v2_def_len, v2_rep_len, false,
                                             page_values))) {
                    LOG_WARN("decode page v2 failed", K(ret));
                  } else {
                    for (int64_t i = 0; OB_SUCC(ret) && i < page_values.count(); ++i) {
                      if (OB_FAIL(values_.push_back(page_values.at(i)))) {
                        LOG_WARN("push value failed", K(ret));
                      }
                    }
                    decoded += v2_num_values;
                  }
                } else if (PQ_CODEC_SNAPPY != rg_col_meta.codec_) {
                  ret = OB_NOT_SUPPORTED;
                  LOG_USER_ERROR(OB_NOT_SUPPORTED, "compressed parquet v2 data pages");
                } else {
                  // v2 compressed: rep/def levels are stored uncompressed at
                  // the page start, only the values region is compressed
                  const int64_t vout_cap = uncomp_size - v2_rep_len - v2_def_len;
                  ObArray<ObObj> page_values;
                  char *vbuf = vout_cap > 0
                      ? static_cast<char *>(scratch.alloc(vout_cap)) : NULL;
                  int64_t vout = 0;
                  const char *vp = data_buf + v2_rep_len + v2_def_len;
                  const int64_t vlen = data_len - v2_rep_len - v2_def_len;
                  if (OB_UNLIKELY(vout_cap < 0 || vlen < 0)) {
                    ret = OB_ERR_UNEXPECTED;
                    LOG_WARN("v2 page size mismatch", K(ret), K(vout_cap), K(vlen));
                  } else if (OB_FAIL(snappy_uncompress(vp, vlen, vbuf, vout_cap, vout))) {
                    LOG_WARN("snappy uncompress v2 values failed", K(ret));
                  } else {
                    // definition levels
                    ObArray<int64_t> def_levels;
                    if (meta_.max_def_level_ > 0) {
                      uint8_t bw = 0;
                      int64_t tmp = meta_.max_def_level_ + 1;
                      while (tmp > 1) {
                        ++bw;
                        tmp >>= 1;
                      }
                      RleDecoder rle(data_buf + v2_rep_len, v2_def_len, bw);
                      int64_t total = 0;
                      while (total < v2_num_values) {
                        int64_t buf[256];
                        const int64_t got = rle.decode(buf, 256);
                        if (got <= 0) {
                          break;
                        }
                        for (int64_t i = 0; OB_SUCC(ret) && i < got && total < v2_num_values; ++i) {
                          if (OB_FAIL(def_levels.push_back(buf[i]))) {
                            LOG_WARN("push def level failed", K(ret));
                          }
                          ++total;
                        }
                      }
                    }
                    // raw values
                    if (OB_SUCC(ret)) {
                      const int64_t non_null = v2_num_values - v2_num_nulls;
                      ObArray<ObObj> raw;
                      const ParquetEncoding venc = static_cast<ParquetEncoding>(v2_encoding);
                      if (PQ_ENC_PLAIN == venc) {
                        if (OB_FAIL(decode_plain_values(vbuf, vout, non_null, raw))) {
                          LOG_WARN("decode raw v2 values failed", K(ret));
                        }
                      } else if (PQ_ENC_PLAIN_DICTIONARY == venc
                                 || PQ_ENC_RLE_DICTIONARY == venc) {
                        if (OB_FAIL(decode_dict_values(vbuf, vout, non_null, raw))) {
                          LOG_WARN("decode dict v2 values failed", K(ret));
                        }
                      } else {
                        ret = OB_NOT_SUPPORTED;
                        LOG_USER_ERROR(OB_NOT_SUPPORTED, "this parquet value encoding");
                      }
                      if (OB_SUCC(ret)) {
                        int64_t vi = 0;
                        for (int64_t i = 0; OB_SUCC(ret) && i < v2_num_values; ++i) {
                          ObObj obj;
                          if (meta_.max_def_level_ > 0
                              && i < def_levels.count()
                              && def_levels.at(i) < meta_.max_def_level_) {
                            obj.set_null();
                          } else if (vi < raw.count()) {
                            obj = raw.at(vi++);
                          } else {
                            obj.set_null();
                          }
                          if (OB_FAIL(page_values.push_back(obj))) {
                            LOG_WARN("push value failed", K(ret));
                          }
                        }
                      }
                      if (OB_SUCC(ret)) {
                        for (int64_t i = 0; OB_SUCC(ret) && i < page_values.count(); ++i) {
                          if (OB_FAIL(values_.push_back(page_values.at(i)))) {
                            LOG_WARN("push value failed", K(ret));
                          }
                        }
                        decoded += v2_num_values;
                      }
                    }
                  }
                }
              }
            }
          }
          pos += hdr_size + comp_size;
        }
      }
    }
    scratch.reset();
  }
  if (OB_SUCC(ret) && decoded != rg_col_meta.num_values_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("row group value count mismatch", K(ret), K(decoded), K(rg_col_meta.num_values_));
  }
  return ret;
}

// ---------------------------------------------------------------------------
// Parquet row reader (multi-file)
// ---------------------------------------------------------------------------
class ObParquetRowReader : public ObIFileRowReader
{
public:
  ObParquetRowReader(ObIAllocator &alloc, const ObIArray<ObString> &paths,
                     const ObIArray<int64_t> &needed_cols)
    : ObIFileRowReader(alloc), file_(struct_arena_), cur_file_(-1), cur_rg_(-1),
      col_readers_(), needed_cols_(), row_idx_(0), rg_rows_(0)
  {
    for (int64_t i = 0; i < paths.count(); ++i) {
      ObString s;
      if (OB_SUCCESS == ob_write_string(struct_arena_, paths.at(i), s, true)) {
        own_paths_.push_back(s);
      }
    }
    for (int64_t i = 0; i < needed_cols.count(); ++i) {
      needed_cols_.push_back(needed_cols.at(i));
    }
  }
  virtual ~ObParquetRowReader() {}
  virtual int open() override;
  virtual int rescan() override;
  virtual int close() override;
  virtual int read_batch(ObObj *cells, int64_t max_rows, int64_t &n_rows) override;
private:
  int open_next_file();
  int load_next_row_group();
  int ensure_col_readers();

  ObArray<ObString> own_paths_;
  ObParquetFile file_;
  int64_t cur_file_;
  int64_t cur_rg_;
  ObArray<ObParquetColumnReader *> col_readers_;
  ObArray<int64_t> needed_cols_;  // indexes into file schema
  int64_t row_idx_;
  int64_t rg_rows_;
};

int ObParquetRowReader::open()
{
  int ret = OB_SUCCESS;
  cur_file_ = -1;
  cur_rg_ = -1;
  row_idx_ = 0;
  rg_rows_ = 0;
  if (OB_FAIL(open_next_file())) {
    LOG_WARN("open first parquet file failed", K(ret));
  }
  return ret;
}

int ObParquetRowReader::rescan()
{
  int ret = OB_SUCCESS;
  total_rows_ = 0;
  for (int64_t i = 0; i < col_readers_.count(); ++i) {
    col_readers_.at(i)->reset_values();
  }
  cur_file_ = -1;
  cur_rg_ = -1;
  row_idx_ = 0;
  rg_rows_ = 0;
  if (OB_FAIL(open_next_file())) {
    LOG_WARN("reopen parquet file failed", K(ret));
  }
  return ret;
}

int ObParquetRowReader::close()
{
  return OB_SUCCESS;
}

int ObParquetRowReader::open_next_file()
{
  int ret = OB_SUCCESS;
  bool opened = false;
  while (!opened && cur_file_ + 1 < own_paths_.count()) {
    ++cur_file_;
    file_.~ObParquetFile();
    new (&file_) ObParquetFile(struct_arena_);
    if (OB_FAIL(file_.open(own_paths_.at(cur_file_)))) {
      LOG_WARN("open parquet failed", K(ret));
    } else if (OB_FAIL(file_.parse_footer())) {
      LOG_WARN("parse footer failed", K(ret));
    } else {
      // validate schema matches cols_
      const ParquetFileMeta &meta = file_.get_meta();
      if (meta.columns_.count() < col_count_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("file has fewer columns than schema", K(ret), K(meta.columns_.count()),
                 K(col_count_));
      } else {
        for (int64_t c = 0; OB_SUCC(ret) && c < col_count_; ++c) {
          const ReadFileColumn &col = cols_.at(c);
          const ParquetColumnMeta &pc = meta.columns_.at(c);
          // name must match
          if (0 != col.name_.case_compare(pc.name_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("column name mismatch", K(ret), K(c), K(col.name_), K(pc.name_));
          }
        }
        if (OB_SUCC(ret)) {
          cur_rg_ = -1;
          row_idx_ = 0;
          rg_rows_ = 0;
          opened = true;
        }
      }
    }
  }
  if (!opened && OB_SUCC(ret)) {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObParquetRowReader::ensure_col_readers()
{
  int ret = OB_SUCCESS;
  col_readers_.reset();
  const ParquetFileMeta &meta = file_.get_meta();
  for (int64_t i = 0; OB_SUCC(ret) && i < needed_cols_.count(); ++i) {
    const int64_t col_idx = needed_cols_.at(i);
    if (col_idx < 0 || col_idx >= meta.columns_.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("bad column index", K(ret), K(col_idx));
    } else {
      ObParquetColumnReader *cr = new (batch_arena_.alloc(sizeof(ObParquetColumnReader)))
          ObParquetColumnReader(batch_arena_, file_, meta.columns_.at(col_idx));
      if (OB_ISNULL(cr)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(col_readers_.push_back(cr))) {
        LOG_WARN("push column reader failed", K(ret));
      }
    }
  }
  return ret;
}

int ObParquetRowReader::load_next_row_group()
{
  int ret = OB_SUCCESS;
  const ParquetFileMeta &meta = file_.get_meta();
  ++cur_rg_;
  if (cur_rg_ >= meta.row_groups_.count()) {
    if (OB_FAIL(open_next_file())) {
      if (OB_ITER_END == ret) {
        // no more files
      } else {
        LOG_WARN("open next file failed", K(ret));
      }
    } else if (OB_FAIL(load_next_row_group())) {
      LOG_WARN("load row group failed", K(ret));
    }
  } else {
    // release the previous row group's buffers and readers
    col_readers_.reset();
    batch_arena_.reset();
    const ParquetRowGroupMeta &rg = meta.row_groups_.at(cur_rg_);
    // update column readers with this row group's codec/offsets
    if (OB_FAIL(ensure_col_readers())) {
      LOG_WARN("ensure col readers failed", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < needed_cols_.count(); ++i) {
      const int64_t chunk_idx = rg.columns_offset_ + needed_cols_.at(i);
      if (OB_UNLIKELY(chunk_idx < 0 || chunk_idx >= meta.chunk_metas_.count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("row group chunk index out of range", K(ret), K(chunk_idx),
                 K(meta.chunk_metas_.count()));
      } else if (OB_FAIL(col_readers_.at(i)->decode_row_group(rg, meta.chunk_metas_.at(chunk_idx)))) {
        LOG_WARN("decode row group failed", K(ret), K(i));
      } else if (col_readers_.at(i)->get_values().count() != rg.num_rows_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("row group value count mismatch", K(ret),
                 K(col_readers_.at(i)->get_values().count()), K(rg.num_rows_));
      }
    }
    if (OB_SUCC(ret)) {
      row_idx_ = 0;
      rg_rows_ = rg.num_rows_;
    }
  }
  return ret;
}

int ObParquetRowReader::read_batch(ObObj *cells, int64_t max_rows, int64_t &n_rows)
{
  int ret = OB_SUCCESS;
  n_rows = 0;
  while (OB_SUCC(ret) && n_rows < max_rows) {
    if (row_idx_ >= rg_rows_) {
      if (OB_FAIL(load_next_row_group())) {
        if (OB_ITER_END == ret) {
          // end of the last file: report a short (possibly empty) batch so the
          // caller can stop on n_rows == 0 instead of seeing an error.
          ret = OB_SUCCESS;
        }
        break;
      }
      continue;
    }
    ObObj *row = cells + n_rows * col_count_;
    for (int64_t c = 0; OB_SUCC(ret) && c < col_count_; ++c) {
      // find needed col reader for output column c
      int64_t reader_idx = -1;
      for (int64_t k = 0; k < needed_cols_.count(); ++k) {
        if (needed_cols_.at(k) == c) {
          reader_idx = k;
          break;
        }
      }
      ObObj out;
      if (reader_idx < 0) {
        out.set_null();
      } else {
        out = col_readers_.at(reader_idx)->get_values().at(row_idx_);
      }
      row[c] = out;
    }
    ++row_idx_;
    ++n_rows;
    ++total_rows_;
  }
  return ret;
}

int infer_parquet_schema(ObIAllocator &alloc, const ObString &path,
                         ObIArray<ReadFileColumn> &cols)
{
  int ret = OB_SUCCESS;
  cols.reset();
  ObArenaAllocator arena("ParquetInfer");
  ObParquetFile file(arena);
  if (OB_FAIL(file.open(path))) {
    LOG_WARN("open parquet failed", K(ret));
  } else if (OB_FAIL(file.parse_footer())) {
    LOG_WARN("parse footer failed", K(ret));
  } else {
    const ParquetFileMeta &meta = file.get_meta();
    for (int64_t i = 0; OB_SUCC(ret) && i < meta.columns_.count(); ++i) {
      const ParquetColumnMeta &pc = meta.columns_.at(i);
      ReadFileColumn col;
      ObString name;
      if (OB_UNLIKELY(OB_ISNULL(pc.name_.ptr()) || pc.name_.length() <= 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("bad parquet column name", K(ret), K(i));
      } else if (OB_FAIL(ob_write_string(alloc, pc.name_, name))) {
        LOG_WARN("copy name failed", K(ret));
      } else {
        col.name_ = name;
        col.nullable_ = (pc.max_def_level_ > 0);
        switch (pc.physical_type_) {
          case PQ_BOOLEAN:
            col.type_ = common::ObTinyIntType;
            break;
          case PQ_INT32:
          case PQ_INT64:
            col.type_ = common::ObIntType;
            break;
          case PQ_FLOAT:
          case PQ_DOUBLE:
            col.type_ = common::ObDoubleType;
            break;
          case PQ_BYTE_ARRAY:
            col.type_ = common::ObVarcharType;
            col.accuracy_.set_length(common::OB_MAX_VARCHAR_LENGTH);
            col.max_length_ = common::OB_MAX_VARCHAR_LENGTH;
            break;
          default:
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "this parquet physical type");
            LOG_WARN("unsupported parquet type", K(ret), K(pc.physical_type_), K(pc.name_));
            break;
        }
        if (OB_SUCC(ret) && OB_FAIL(cols.push_back(col))) {
          LOG_WARN("push column failed", K(ret));
        }
      }
    }
  }
  return ret;
}

int create_parquet_row_reader(ObIAllocator &alloc, const ObIArray<ObString> &paths,
                              const ObIArray<ReadFileColumn> &cols,
                              const ObTimeZoneInfo *tz_info, ObIFileRowReader *&reader)
{
  int ret = OB_SUCCESS;
  reader = NULL;
  // all columns are needed initially; the SQL layer may prune columns by
  // passing a smaller `cols` array -- the reader maps output column index to
  // schema index by name
  ObArray<int64_t> needed;
  ObArray<ObString> first_file_names;
  ObArenaAllocator tmp("ParquetNames");
  {
    ObParquetFile probe(tmp);
    if (OB_FAIL(probe.open(paths.at(0)))) {
      LOG_WARN("open parquet failed", K(ret));
    } else if (OB_FAIL(probe.parse_footer())) {
      LOG_WARN("parse footer failed", K(ret));
    } else {
      const ParquetFileMeta &meta = probe.get_meta();
      for (int64_t c = 0; OB_SUCC(ret) && c < cols.count(); ++c) {
        int64_t idx = -1;
        for (int64_t k = 0; k < meta.columns_.count(); ++k) {
          if (0 == meta.columns_.at(k).name_.case_compare(cols.at(c).name_)) {
            idx = k;
            break;
          }
        }
        if (idx < 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("column not found in parquet file", K(ret), K(cols.at(c).name_));
        } else if (OB_FAIL(needed.push_back(idx))) {
          LOG_WARN("push needed failed", K(ret));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    ObParquetRowReader *r = new (alloc.alloc(sizeof(ObParquetRowReader)))
        ObParquetRowReader(alloc, paths, needed);
    if (OB_ISNULL(r)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(r->init(cols.count(), cols, tz_info))) {
      LOG_WARN("init parquet reader failed", K(ret));
    } else {
      reader = r;
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
