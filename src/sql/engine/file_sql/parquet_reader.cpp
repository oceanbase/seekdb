#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/file_sql/parquet_reader.h"
#include "share/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if defined(__linux__)
#include <unistd.h>
#endif


namespace oceanbase::sql::file_sql {
using namespace common;

namespace {

// ---------------------------------------------------------------------------
// Thrift compact protocol (only the subset used by the Parquet footer/pages).
// ---------------------------------------------------------------------------
class Compact {
public:
  Compact(const unsigned char *data, size_t size) : p_(data), end_(data + size) {}
  const unsigned char *pos() const { return p_; }
  bool ok() const { return ok_; }
  bool at_end() const { return p_ == end_; }

  uint64_t varint()
  {
    uint64_t value = 0;
    int shift = 0;
    while (ok_ && p_ < end_) {
      const unsigned char c = *p_++;
      value |= static_cast<uint64_t>(c & 0x7f) << shift;
      if (0 == (c & 0x80)) return value;
      shift += 7;
      if (shift > 63) break;
    }
    ok_ = false;
    return 0;
  }
  int64_t zigzag()
  {
    const uint64_t value = varint();
    return static_cast<int64_t>((value >> 1) ^ -(value & 1));
  }
  int32_t i32() { return static_cast<int32_t>(zigzag()); }
  int64_t i64() { return zigzag(); }
  bool boolean() { return varint() != 0 || true; }

  // Returns false when the struct ended; sets type/id for a real field.
  bool field(int32_t &type, int32_t &id)
  {
    if (!ok_ || p_ >= end_) { ok_ = false; return false; }
    const unsigned char header = *p_++;
    if (0 == header) return false;
    type = header & 0x0f;
    const int32_t delta = (header >> 4) & 0x0f;
    id = 0 == delta ? static_cast<int32_t>(zigzag()) : last_ + delta;
    last_ = id;
    return true;
  }
  void reset_struct() { last_ = 0; }
  int32_t last() const { return last_; }
  void set_last(int32_t value) { last_ = value; }

  bool bytes(std::string &out)
  {
    const uint64_t size = varint();
    if (!ok_ || p_ + size > end_) { ok_ = false; return false; }
    out.assign(reinterpret_cast<const char *>(p_), size);
    p_ += size;
    return true;
  }
  bool list(int32_t &size, int32_t &elem_type)
  {
    if (!ok_ || p_ >= end_) { ok_ = false; return false; }
    const unsigned char header = *p_++;
    size = (header >> 4) & 0x0f;
    elem_type = header & 0x0f;
    if (15 == size) size = static_cast<int32_t>(varint());
    return ok_;
  }
  bool skip(int32_t type)
  {
    // Nested structs restart the field-id delta tracking; preserve the
    // enclosing struct's state.
    const int32_t saved = last_;
    const bool result = skip_impl(type);
    last_ = saved;
    return result;
  }
  bool skip_impl(int32_t type)
  {
    switch (type) {
      case 1: case 2: return true;                        // bool value is in the header
      case 3: if (p_ < end_) { ++p_; return true; } ok_ = false; return false;
      case 4: case 5: case 6: varint(); return ok_;
      case 7: if (p_ + 8 <= end_) { p_ += 8; return true; } ok_ = false; return false;
      case 8: { std::string ignored; return bytes(ignored); }
      case 9: case 10: {
        int32_t size = 0, elem = 0;
        if (!list(size, elem)) return false;
        for (int32_t i = 0; ok_ && i < size; ++i) skip(elem);
        return ok_;
      }
      case 11: {
        const uint64_t size = varint();
        if (!ok_ || 0 == size) return ok_;
        if (p_ >= end_) { ok_ = false; return false; }
        const unsigned char kinds = *p_++;
        const int32_t key_type = kinds >> 4, value_type = kinds & 0x0f;
        for (uint64_t i = 0; ok_ && i < size; ++i) {
          skip(key_type);
          skip(value_type);
        }
        return ok_;
      }
      case 12: {
        reset_struct();
        int32_t field_type = 0, field_id = 0;
        while (ok_ && field(field_type, field_id)) skip(field_type);
        return ok_;
      }
      default: ok_ = false; return false;
    }
  }

private:
  const unsigned char *p_;
  const unsigned char *end_;
  bool ok_ = true;
  int32_t last_ = 0;
};

// ---------------------------------------------------------------------------
// Snappy raw decompression (Parquet's default codec).
// ---------------------------------------------------------------------------
int snappy_decompress(const unsigned char *src, size_t size, std::string &out)
{
  size_t i = 0;
  uint64_t expected = 0;
  int shift = 0;
  while (i < size) {
    const unsigned char c = src[i++];
    expected |= static_cast<uint64_t>(c & 0x7f) << shift;
    if (0 == (c & 0x80)) break;
    shift += 7;
    if (shift > 63) return OB_INVALID_DATA;
  }
  if (i >= size) return OB_INVALID_DATA;
  out.clear();
  out.reserve(expected);
  while (i < size && out.size() < expected) {
    const unsigned char tag = src[i++];
    const int type = tag & 0x03;
    if (0 == type) {
      size_t length = tag >> 2;
      if (length < 60) {
        ++length;
      } else {
        const size_t extra = length - 59;
        if (i + extra > size) return OB_INVALID_DATA;
        length = 0;
        for (size_t k = 0; k < extra; ++k) length |= static_cast<size_t>(src[i + k]) << (8 * k);
        length += 1;
        i += extra;
      }
      if (i + length > size) return OB_INVALID_DATA;
      out.append(reinterpret_cast<const char *>(src + i), length);
      i += length;
    } else {
      size_t length = 0, offset = 0;
      if (1 == type) {
        length = ((tag >> 2) & 0x07) + 4;
        if (i >= size) return OB_INVALID_DATA;
        offset = (static_cast<size_t>(tag >> 5) << 8) | src[i++];
      } else if (2 == type) {
        length = (tag >> 2) + 1;
        if (i + 2 > size) return OB_INVALID_DATA;
        offset = src[i] | (static_cast<size_t>(src[i + 1]) << 8);
        i += 2;
      } else {
        length = (tag >> 2) + 1;
        if (i + 4 > size) return OB_INVALID_DATA;
        offset = src[i] | (static_cast<size_t>(src[i + 1]) << 8) |
                 (static_cast<size_t>(src[i + 2]) << 16) |
                 (static_cast<size_t>(src[i + 3]) << 24);
        i += 4;
      }
      if (0 == offset || offset > out.size()) return OB_INVALID_DATA;
      for (size_t k = 0; k < length; ++k) out.push_back(out[out.size() - offset]);
    }
  }
  if (out.size() != expected) return OB_INVALID_DATA;
  return OB_SUCCESS;
}

// ---------------------------------------------------------------------------
// Value formatting helpers.
// ---------------------------------------------------------------------------
std::string civil_date(int64_t days)
{
  int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t year = static_cast<int64_t>(yoe) + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t month = mp + (mp < 10 ? 3 : -9);
  year += (month <= 2);
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04lld-%02u-%02u", static_cast<long long>(year), month, day);
  return buffer;
}

std::string date_time_text(int64_t seconds)
{
  int64_t days = seconds / 86400;
  int64_t rest = seconds % 86400;
  if (rest < 0) { rest += 86400; --days; }
  char time[16];
  snprintf(time, sizeof(time), "%02lld:%02lld:%02lld",
           static_cast<long long>(rest / 3600),
           static_cast<long long>((rest / 60) % 60),
           static_cast<long long>(rest % 60));
  return civil_date(days) + " " + time;
}

enum PhysicalType {
  PARQUET_BOOLEAN = 0,
  PARQUET_INT32 = 1,
  PARQUET_INT64 = 2,
  PARQUET_INT96 = 3,
  PARQUET_FLOAT = 4,
  PARQUET_DOUBLE = 5,
  PARQUET_BYTE_ARRAY = 6,
  PARQUET_FIXED = 7,
};

struct ColumnInfo {
  std::string name;
  Type type = Type::text;
  bool nullable = true;
  int32_t physical = -1;
  int32_t converted = -1;
  bool logical_date = false;
  bool logical_timestamp = false;
  bool logical_time = false;
  bool logical_string = false;
  int32_t time_unit = 0;  // 1 millis, 2 micros, 3 nanos
  int32_t fixed_length = 0;
};

struct ChunkMeta {
  int64_t start_offset = -1;
  int64_t data_offset = -1;
  int64_t dict_offset = -1;
  int64_t compressed_size = 0;
  int64_t num_values = 0;
  int32_t codec = 0;
  int32_t physical = -1;
};

struct PageHeader {
  int32_t type = -1;
  int32_t uncompressed_size = 0;
  int32_t compressed_size = 0;
  int32_t num_values = 0;
  int32_t num_rows = 0;
  int32_t encoding = 0;
  int32_t def_encoding = 2;
  int32_t rep_encoding = 2;
  bool v2_compressed = false;
  int32_t dict_num_values = 0;
};

struct RowGroup {
  int64_t num_rows = 0;
  std::vector<ChunkMeta> chunks;
};

struct Cursor {
  ChunkMeta meta;
  int64_t page_offset = 0;
  int64_t end_offset = 0;
  int64_t rows_read = 0;
  std::vector<std::string> dictionary;
  std::vector<Field> values;
  int64_t page_start_row = 0;
};

bool is_plain_encoding(int32_t encoding) { return 0 == encoding; }
bool is_dictionary_encoding(int32_t encoding) { return 2 == encoding || 7 == encoding; }

}  // namespace

struct ParquetReader::Impl {
  int fd_ = -1;
  int64_t file_size_ = 0;
  std::function<bool()> keep_running_;
  std::vector<ColumnInfo> columns_;
  std::vector<RowGroup> groups_;
  int64_t num_rows_ = 0;
  bool loaded_ = false;
  size_t group_index_ = 0;
  int64_t row_in_group_ = 0;
  bool begun_ = false;
  std::vector<Cursor> cursors_;

  bool stopped() { return keep_running_ && !keep_running_(); }

  int read_at(int64_t offset, size_t size, std::string &out)
  {
#if defined(__linux__)
    if (offset < 0 || offset + static_cast<int64_t>(size) > file_size_) return OB_INVALID_DATA;
    out.resize(size);
    size_t done = 0;
    while (done < size) {
      const ssize_t n = ::pread(fd_, out.data() + done, size - done, offset + static_cast<int64_t>(done));
      if (n < 0) return OB_IO_ERROR;
      if (0 == n) return OB_INVALID_DATA;
      done += static_cast<size_t>(n);
    }
    return OB_SUCCESS;
#else
    return OB_NOT_SUPPORTED;
#endif
  }

  int parse_page_header(int64_t offset, PageHeader &header, size_t &header_size)
  {
    int ret = OB_SUCCESS;
    (void)ret;
    size_t window = 4096;
    for (int attempt = 0; attempt < 4; ++attempt, window *= 4) {
      if (offset + static_cast<int64_t>(window) > file_size_) {
        window = static_cast<size_t>(file_size_ - offset);
      }
      std::string buffer;
      if (OB_FAIL(read_at(offset, window, buffer))) return OB_INVALID_DATA;
      Compact reader(reinterpret_cast<const unsigned char *>(buffer.data()), buffer.size());
      reader.reset_struct();
      int32_t field_type = 0, field_id = 0;
      bool complete = false;
      while (reader.ok() && reader.field(field_type, field_id)) {
        switch (field_id) {
          case 1: header.type = reader.i32(); break;
          case 2: header.uncompressed_size = reader.i32(); break;
          case 3: header.compressed_size = reader.i32(); break;
          case 5: {
            const int32_t saved = reader.last();
            reader.reset_struct();
            int32_t t = 0, id = 0;
            while (reader.ok() && reader.field(t, id)) {
              switch (id) {
                case 1: header.num_values = reader.i32(); break;
                case 2: header.encoding = reader.i32(); break;
                case 3: header.def_encoding = reader.i32(); break;
                case 4: header.rep_encoding = reader.i32(); break;
                default: reader.skip(t);
              }
            }
            if (reader.ok()) complete = true;
            reader.set_last(saved);
            break;
          }
          case 7: {
            const int32_t saved = reader.last();
            reader.reset_struct();
            int32_t t = 0, id = 0;
            while (reader.ok() && reader.field(t, id)) {
              if (1 == id) header.dict_num_values = reader.i32();
              else reader.skip(t);
            }
            reader.set_last(saved);
            break;
          }
          case 8: {
            const int32_t saved = reader.last();
            reader.reset_struct();
            int32_t t = 0, id = 0;
            while (reader.ok() && reader.field(t, id)) {
              switch (id) {
                case 1: header.num_values = reader.i32(); break;
                case 3: header.num_rows = reader.i32(); break;
                case 4: header.encoding = reader.i32(); break;
                case 5: header.def_encoding = reader.i32(); break;
                case 6: header.rep_encoding = reader.i32(); break;
                case 7: header.v2_compressed = true; if (1 == t || 2 == t) header.v2_compressed = 1 == t; else reader.skip(t); break;
                default: reader.skip(t);
              }
            }
            reader.set_last(saved);
            break;
          }
          default: reader.skip(field_type);
        }
      }
      if (reader.ok()) {
        complete = true;
        // The outer struct is terminated by a zero byte; `field` returns
        // false at that point and the reader still has a valid position.
      }
      if (complete) {
        header_size = static_cast<size_t>(reader.pos() - reinterpret_cast<const unsigned char *>(buffer.data()));
        return OB_SUCCESS;
      }
      if (window >= static_cast<size_t>(file_size_ - offset)) break;
    }
    return OB_INVALID_DATA;
  }

  int decode_plain(const unsigned char *data, size_t size, size_t &pos,
                   int64_t count, const ColumnInfo &column, std::vector<std::string> &out)
  {
    out.clear();
    out.reserve(static_cast<size_t>(count));
    switch (column.physical) {
      case PARQUET_INT32: {
        if (pos + static_cast<size_t>(count) * 4 > size) return OB_INVALID_DATA;
        for (int64_t i = 0; i < count; ++i) {
          int32_t value = 0;
          memcpy(&value, data + pos, 4); pos += 4;
          if (column.logical_date || 6 == column.converted) {
            out.push_back(civil_date(value));
          } else if (column.logical_time || 7 == column.converted || 8 == column.converted) {
            char buffer[32];
            if (7 == column.converted || 1 == column.time_unit) {
              snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", value / 3600000,
                       (value / 60000) % 60, (value / 1000) % 60);
            } else {
              const int64_t seconds = value / 1000000;
              snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld",
                       static_cast<long long>(seconds / 3600),
                       static_cast<long long>((seconds / 60) % 60),
                       static_cast<long long>(seconds % 60));
            }
            out.emplace_back(buffer);
          } else {
            out.push_back(std::to_string(value));
          }
        }
        break;
      }
      case PARQUET_INT64: {
        if (pos + static_cast<size_t>(count) * 8 > size) return OB_INVALID_DATA;
        for (int64_t i = 0; i < count; ++i) {
          int64_t value = 0;
          memcpy(&value, data + pos, 8); pos += 8;
          if (column.logical_timestamp || 9 == column.converted || 10 == column.converted) {
            int64_t seconds = value;
            const int32_t unit = 9 == column.converted ? 1 : (10 == column.converted ? 2 : column.time_unit);
            if (1 == unit) seconds /= 1000;
            else if (2 == unit) seconds /= 1000000;
            else if (3 == unit) seconds /= 1000000000;
            out.push_back(date_time_text(seconds));
          } else if (column.logical_time) {
            const int64_t seconds = value / (1 == column.time_unit ? 1000 :
                                           (2 == column.time_unit ? 1000000 : 1000000000));
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld",
                     static_cast<long long>(seconds / 3600),
                     static_cast<long long>((seconds / 60) % 60),
                     static_cast<long long>(seconds % 60));
            out.emplace_back(buffer);
          } else {
            out.push_back(std::to_string(value));
          }
        }
        break;
      }
      case PARQUET_INT96: {
        if (pos + static_cast<size_t>(count) * 12 > size) return OB_INVALID_DATA;
        for (int64_t i = 0; i < count; ++i) {
          int64_t nanos = 0;
          int32_t julian = 0;
          memcpy(&nanos, data + pos, 8);
          memcpy(&julian, data + pos + 8, 4);
          pos += 12;
          const int64_t seconds = nanos / 1000000000 + (static_cast<int64_t>(julian) - 2440588) * 86400;
          out.push_back(date_time_text(seconds));
        }
        break;
      }
      case PARQUET_FLOAT: {
        if (pos + static_cast<size_t>(count) * 4 > size) return OB_INVALID_DATA;
        for (int64_t i = 0; i < count; ++i) {
          float value = 0;
          memcpy(&value, data + pos, 4); pos += 4;
          char buffer[64];
          snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
          out.emplace_back(buffer);
        }
        break;
      }
      case PARQUET_DOUBLE: {
        if (pos + static_cast<size_t>(count) * 8 > size) return OB_INVALID_DATA;
        for (int64_t i = 0; i < count; ++i) {
          double value = 0;
          memcpy(&value, data + pos, 8); pos += 8;
          char buffer[64];
          snprintf(buffer, sizeof(buffer), "%.17g", value);
          out.emplace_back(buffer);
        }
        break;
      }
      case PARQUET_BOOLEAN: {
        if (pos + static_cast<size_t>((count + 7) / 8) > size) return OB_INVALID_DATA;
        for (int64_t i = 0; i < count; ++i) {
          const bool value = 0 != (data[pos + static_cast<size_t>(i / 8)] & (1 << (i % 8)));
          out.push_back(value ? "true" : "false");
        }
        pos += static_cast<size_t>((count + 7) / 8);
        break;
      }
      case PARQUET_BYTE_ARRAY: {
        for (int64_t i = 0; i < count; ++i) {
          if (pos + 4 > size) return OB_INVALID_DATA;
          int32_t length = 0;
          memcpy(&length, data + pos, 4); pos += 4;
          if (length < 0 || pos + static_cast<size_t>(length) > size) return OB_INVALID_DATA;
          out.emplace_back(reinterpret_cast<const char *>(data + pos), static_cast<size_t>(length));
          pos += static_cast<size_t>(length);
        }
        break;
      }
      case PARQUET_FIXED: {
        const size_t length = static_cast<size_t>(column.fixed_length);
        if (length == 0 || pos + length * static_cast<size_t>(count) > size) return OB_INVALID_DATA;
        for (int64_t i = 0; i < count; ++i) {
          out.emplace_back(reinterpret_cast<const char *>(data + pos), length);
          pos += length;
        }
        break;
      }
      default: return OB_NOT_SUPPORTED;
    }
    return OB_SUCCESS;
  }

  int decode_rle(const unsigned char *data, size_t size, size_t &pos,
                 int32_t bit_width, int64_t count, std::vector<int32_t> &out)
  {
    out.clear();
    out.reserve(static_cast<size_t>(count));
    while (out.size() < static_cast<size_t>(count)) {
      if (stopped()) return OB_ERR_INTERRUPTED;
      if (pos >= size) return OB_INVALID_DATA;
      uint64_t header = 0;
      int shift = 0;
      const size_t header_start = pos;
      while (pos < size) {
        const unsigned char c = data[pos++];
        header |= static_cast<uint64_t>(c & 0x7f) << shift;
        if (0 == (c & 0x80)) break;
        shift += 7;
        if (shift > 63) return OB_INVALID_DATA;
      }
      if (pos > size) return OB_INVALID_DATA;
      if (header & 1) {
        const size_t groups = static_cast<size_t>(header >> 1);
        if (bit_width <= 0 || bit_width > 32) return OB_INVALID_DATA;
        const size_t bytes = groups * static_cast<size_t>(bit_width);
        if (pos + bytes > size) return OB_INVALID_DATA;
        for (size_t group = 0; group < groups; ++group) {
          uint32_t value = 0;
          for (int32_t byte = 0; byte < bit_width; ++byte) {
            value |= static_cast<uint32_t>(data[pos + group * bit_width + byte]) << (8 * byte);
          }
          for (int32_t bit = 0; bit < 8 && out.size() < static_cast<size_t>(count); ++bit) {
            out.push_back(static_cast<int32_t>((value >> bit) & 1u));
          }
        }
        pos += bytes;
      } else {
        const size_t run = static_cast<size_t>(header >> 1);
        int64_t value = 0;
        if (bit_width > 0) {
          if (pos + static_cast<size_t>(bit_width) > size) return OB_INVALID_DATA;
          for (int32_t byte = 0; byte < bit_width; ++byte) {
            value |= static_cast<int64_t>(data[pos++]) << (8 * byte);
          }
        }
        for (size_t i = 0; i < run && out.size() < static_cast<size_t>(count); ++i) {
          out.push_back(static_cast<int32_t>(value));
        }
      }
      if (pos == header_start) return OB_INVALID_DATA;
    }
    return OB_SUCCESS;
  }

  int decode_values(const unsigned char *data, size_t size, size_t &pos, int32_t encoding,
                    int64_t count, const ColumnInfo &column, Cursor &cursor,
                    std::vector<std::string> &out)
  {
    int ret = OB_SUCCESS;
    (void)ret;
    if (is_plain_encoding(encoding)) {
      return decode_plain(data, size, pos, count, column, out);
    }
    if (!is_dictionary_encoding(encoding)) return OB_NOT_SUPPORTED;
    if (pos >= size) return OB_INVALID_DATA;
    const int32_t bit_width = static_cast<int32_t>(data[pos++]);
    std::vector<int32_t> indexes;
    if (OB_FAIL(decode_rle(data, size, pos, bit_width, count, indexes))) return OB_INVALID_DATA;
    out.clear();
    out.reserve(static_cast<size_t>(count));
    for (size_t i = 0; i < indexes.size(); ++i) {
      const int32_t index = indexes[i];
      if (index < 0 || static_cast<size_t>(index) >= cursor.dictionary.size()) return OB_INVALID_DATA;
      out.push_back(cursor.dictionary[static_cast<size_t>(index)]);
    }
    return OB_SUCCESS;
  }

  int load_page(Cursor &cursor, const ColumnInfo &column)
  {
    int ret = OB_SUCCESS;
    (void)ret;
    PageHeader header;
    size_t header_size = 0;
    while (cursor.page_offset > 0 && cursor.page_offset < cursor.end_offset) {
      if (stopped()) return OB_ERR_INTERRUPTED;
      if (OB_FAIL(parse_page_header(cursor.page_offset, header, header_size))) return OB_INVALID_DATA;
      std::string payload;
      if (OB_FAIL(read_at(cursor.page_offset + static_cast<int64_t>(header_size),
                          static_cast<size_t>(header.compressed_size), payload))) {
        return OB_INVALID_DATA;
      }
      const int64_t next_offset = cursor.page_offset + static_cast<int64_t>(header_size) +
                                  static_cast<int64_t>(header.compressed_size);
      if (next_offset <= cursor.page_offset) return OB_INVALID_DATA;
      cursor.page_offset = next_offset;
      if (2 == header.type) {  // dictionary page
        std::string raw;
        if (0 == cursor.meta.codec) {
          raw = std::move(payload);
        } else if (1 == cursor.meta.codec) {
          int code = snappy_decompress(reinterpret_cast<const unsigned char *>(payload.data()),
                                       payload.size(), raw);
          if (OB_SUCCESS != code) return code;
        } else {
          return OB_NOT_SUPPORTED;
        }
        size_t pos = 0;
        if (OB_FAIL(decode_plain(reinterpret_cast<const unsigned char *>(raw.data()), raw.size(),
                                 pos, header.dict_num_values, column, cursor.dictionary))) {
          return OB_INVALID_DATA;
        }
        continue;
      }
      if (0 == header.type) {
        std::string raw;
        if (0 == cursor.meta.codec) {
          raw = std::move(payload);
        } else if (1 == cursor.meta.codec) {
          int code = snappy_decompress(reinterpret_cast<const unsigned char *>(payload.data()),
                                       payload.size(), raw);
          if (OB_SUCCESS != code) return code;
        } else {
          return OB_NOT_SUPPORTED;
        }
        const unsigned char *data = reinterpret_cast<const unsigned char *>(raw.data());
        size_t pos = 0;
        std::vector<int32_t> defs;
        if (column.nullable) {
          if (pos + 4 > raw.size()) return OB_INVALID_DATA;
          int32_t level_length = 0;
          memcpy(&level_length, data + pos, 4); pos += 4;
          if (level_length < 0 || pos + static_cast<size_t>(level_length) > raw.size()) return OB_INVALID_DATA;
          size_t level_pos = 0;
          if (OB_FAIL(decode_rle(data + pos, static_cast<size_t>(level_length), level_pos, 1,
                                 header.num_values, defs))) return OB_INVALID_DATA;
          pos = 4 + static_cast<size_t>(level_length);
        }
        int64_t present = 0;
        for (size_t i = 0; i < defs.size(); ++i) if (defs[i] == 1) ++present;
        if (!column.nullable) present = header.num_values;
        std::vector<std::string> raw_values;
        if (OB_FAIL(decode_values(data, raw.size(), pos, header.encoding, present, column,
                                  cursor, raw_values))) return OB_INVALID_DATA;
        cursor.values.clear();
        cursor.values.reserve(static_cast<size_t>(header.num_values));
        int64_t next_value = 0;
        for (int64_t i = 0; i < header.num_values; ++i) {
          Field field;
          const bool is_null = column.nullable && !defs.empty() &&
                               defs[static_cast<size_t>(i)] != 1;
          if (!is_null) {
            if (next_value >= static_cast<int64_t>(raw_values.size())) return OB_INVALID_DATA;
            field.text = std::move(raw_values[static_cast<size_t>(next_value++)]);
            field.quoted = true;
          }
          cursor.values.push_back(std::move(field));
        }
        cursor.page_start_row = cursor.rows_read;
        cursor.rows_read += header.num_values;
        return OB_SUCCESS;
      }
      if (3 == header.type) {
        const unsigned char *data = reinterpret_cast<const unsigned char *>(payload.data());
        size_t pos = 0;
        std::vector<int32_t> defs;
        if (column.nullable) {
          if (OB_FAIL(decode_rle(data, payload.size(), pos, 1, header.num_values, defs))) {
            return OB_INVALID_DATA;
          }
        }
        int64_t present = header.num_values;
        if (column.nullable) {
          present = 0;
          for (size_t i = 0; i < defs.size(); ++i) if (defs[i] == 1) ++present;
        }
        std::string raw;
        const size_t value_size = payload.size() - pos;
        if (0 == cursor.meta.codec || !header.v2_compressed) {
          raw.assign(payload.data() + pos, value_size);
        } else if (1 == cursor.meta.codec) {
          int code = snappy_decompress(data + pos, value_size, raw);
          if (OB_SUCCESS != code) return code;
        } else {
          return OB_NOT_SUPPORTED;
        }
        size_t value_pos = 0;
        std::vector<std::string> raw_values;
        if (OB_FAIL(decode_values(reinterpret_cast<const unsigned char *>(raw.data()), raw.size(),
                                  value_pos, header.encoding, present, column, cursor, raw_values))) {
          return OB_INVALID_DATA;
        }
        cursor.values.clear();
        cursor.values.reserve(static_cast<size_t>(header.num_values));
        int64_t next_value = 0;
        for (int64_t i = 0; i < header.num_values; ++i) {
          Field field;
          const bool is_null = column.nullable && !defs.empty() && defs[static_cast<size_t>(i)] != 1;
          if (!is_null) {
            if (next_value >= static_cast<int64_t>(raw_values.size())) return OB_INVALID_DATA;
            field.text = std::move(raw_values[static_cast<size_t>(next_value++)]);
            field.quoted = true;
          }
          cursor.values.push_back(std::move(field));
        }
        cursor.page_start_row = cursor.rows_read;
        cursor.rows_read += header.num_values;
        return OB_SUCCESS;
      }
      // Skip index pages and unknown page kinds.
    }
    return OB_ITER_END;
  }

  int value_at(Cursor &cursor, const ColumnInfo &column, int64_t row, Field &out)
  {
    while (row >= cursor.page_start_row + static_cast<int64_t>(cursor.values.size())) {
      if (cursor.rows_read >= cursor.meta.num_values) return OB_INVALID_DATA;
      const int code = load_page(cursor, column);
      if (OB_ITER_END == code) return OB_INVALID_DATA;
      if (OB_SUCCESS != code) return code;
      if (0 == cursor.values.size()) return OB_INVALID_DATA;
    }
    const int64_t offset = row - cursor.page_start_row;
    if (offset < 0 || offset >= static_cast<int64_t>(cursor.values.size())) return OB_INVALID_DATA;
    out = cursor.values[static_cast<size_t>(offset)];
    return OB_SUCCESS;
  }

  int parse_logical_type(Compact &reader, int32_t field_type, ColumnInfo &column)
  {
    if (12 != field_type) {
      reader.skip(field_type);
      return reader.ok() ? OB_SUCCESS : OB_INVALID_DATA;
    }
    reader.reset_struct();
    int32_t type = 0, id = 0;
    while (reader.ok() && reader.field(type, id)) {
      switch (id) {
        case 1: column.logical_string = true; reader.skip(type); break;
        case 6: column.logical_date = true; reader.skip(type); break;
        case 7: column.logical_time = true; reader.skip(type); break;
        case 8: {
          reader.reset_struct();
          int32_t t = 0, i = 0;
          while (reader.ok() && reader.field(t, i)) {
            if (2 == i && 12 == t) {
              column.logical_timestamp = true;
              reader.reset_struct();
              int32_t ut = 0, uid = 0;
              while (reader.ok() && reader.field(ut, uid)) {
                if (1 == uid) column.time_unit = 1;
                else if (2 == uid) column.time_unit = 2;
                else if (3 == uid) column.time_unit = 3;
                reader.skip(ut);
              }
            } else {
              reader.skip(t);
            }
          }
          break;
        }
        default: reader.skip(type);
      }
    }
    return reader.ok() ? OB_SUCCESS : OB_INVALID_DATA;
  }

  int parse_schema_element(Compact &reader, ColumnInfo &column, int32_t &num_children,
                           int32_t &repetition)
  {
    int ret = OB_SUCCESS;
    (void)ret;
    reader.reset_struct();
    int32_t type = 0, id = 0;
    num_children = 0;
    repetition = -1;
    while (reader.ok() && reader.field(type, id)) {
      switch (id) {
        case 1: column.physical = reader.i32(); break;
        case 2: column.fixed_length = reader.i32(); break;
        case 3: repetition = reader.i32(); break;
        case 4: reader.bytes(column.name); break;
        case 5: num_children = reader.i32(); break;
        case 6: column.converted = reader.i32(); break;
        case 10: {
          const int32_t saved = reader.last();
          if (OB_FAIL(parse_logical_type(reader, type, column))) return OB_INVALID_DATA;
          reader.set_last(saved);
          break;
        }
        default: reader.skip(type);
      }
    }
    return reader.ok() ? OB_SUCCESS : OB_INVALID_DATA;
  }

  int parse_column_meta(Compact &reader, ChunkMeta &meta)
  {
    reader.reset_struct();
    int32_t type = 0, id = 0;
    while (reader.ok() && reader.field(type, id)) {
      switch (id) {
        case 1: meta.physical = reader.i32(); break;
        case 4: meta.codec = reader.i32(); break;
        case 5: meta.num_values = reader.i64(); break;
        case 7: meta.compressed_size = reader.i64(); break;
        case 9: meta.data_offset = reader.i64(); break;
        case 11: meta.dict_offset = reader.i64(); break;
        default: reader.skip(type);
      }
    }
    return reader.ok() ? OB_SUCCESS : OB_INVALID_DATA;
  }

  int parse_row_group(Compact &reader, RowGroup &group)
  {
    int ret = OB_SUCCESS;
    (void)ret;
    reader.reset_struct();
    int32_t type = 0, id = 0;
    while (reader.ok() && reader.field(type, id)) {
      if (1 == id && 9 == type) {
        int32_t size = 0, elem = 0;
        if (!reader.list(size, elem)) return OB_INVALID_DATA;
        for (int32_t i = 0; ok_reader(reader) && i < size; ++i) {
          if (12 != elem) return OB_NOT_SUPPORTED;
          ChunkMeta meta;
          const int32_t chunk_saved = reader.last();
          reader.reset_struct();
          int32_t t = 0, fid = 0;
          while (reader.ok() && reader.field(t, fid)) {
            if (2 == fid) meta.start_offset = reader.i64();
            else if (3 == fid && 12 == t) {
              const int32_t meta_saved = reader.last();
              if (OB_FAIL(parse_column_meta(reader, meta))) return OB_INVALID_DATA;
              reader.set_last(meta_saved);
            } else reader.skip(t);
          }
          reader.set_last(chunk_saved);
          group.chunks.push_back(meta);
        }
      } else if (2 == id) {
        reader.skip(type);
      } else if (3 == id) {
        group.num_rows = reader.i64();
      } else {
        reader.skip(type);
      }
    }
    return reader.ok() ? OB_SUCCESS : OB_INVALID_DATA;
  }

  static bool ok_reader(const Compact &reader) { return reader.ok(); }

  int parse_footer(std::vector<std::pair<ColumnInfo, int32_t>> &schema, int32_t &repetitions)
  {
    int ret = OB_SUCCESS;
    (void)ret;
    std::string footer;
    int64_t footer_size = -1;
    {
      std::string tail;
      int read_ret = read_at(file_size_ - 8, 8, tail);
      if (OB_SUCCESS != read_ret) return OB_INVALID_DATA;
      if (0 != memcmp(tail.data() + 4, "PAR1", 4)) return OB_INVALID_DATA;
      int32_t size = 0;
      memcpy(&size, tail.data(), 4);
      footer_size = static_cast<int64_t>(size);
    }
    if (footer_size < 0) return OB_INVALID_DATA;
    if (OB_FAIL(read_at(file_size_ - 8 - footer_size, static_cast<size_t>(footer_size), footer))) {
      return OB_INVALID_DATA;
    }
    Compact reader(reinterpret_cast<const unsigned char *>(footer.data()), footer.size());
    reader.reset_struct();
    int32_t type = 0, id = 0;
    int32_t schema_fields = 0;
    bool flat = true;
    while (reader.ok() && reader.field(type, id)) {
      switch (id) {
        case 2: {
          int32_t size = 0, elem = 0;
          if (!reader.list(size, elem)) return OB_INVALID_DATA;
          if (12 != elem) return OB_INVALID_DATA;
          for (int32_t i = 0; i < size; ++i) {
            ColumnInfo column;
            int32_t children = 0, repetition = -1;
            const int32_t saved = reader.last();
            if (OB_FAIL(parse_schema_element(reader, column, children, repetition))) {
              return OB_INVALID_DATA;
            }
            reader.set_last(saved);
            if (0 == i) {
              if (children <= 0) flat = false;
            } else if (children > 0 || repetition == 2) {
              flat = false;
            }
            if (i > 0) {
              schema.emplace_back(column, repetition);
              repetitions++;
            }
            if (!reader.ok()) return OB_INVALID_DATA;
          }
          schema_fields = size;
          break;
        }
        case 3: num_rows_ = reader.i64(); break;
        case 4: {
          int32_t size = 0, elem = 0;
          if (!reader.list(size, elem)) return OB_INVALID_DATA;
          if (12 != elem) return OB_INVALID_DATA;
          for (int32_t i = 0; i < size; ++i) {
            RowGroup group;
            const int32_t saved = reader.last();
            if (OB_FAIL(parse_row_group(reader, group))) return OB_INVALID_DATA;
            reader.set_last(saved);
            groups_.push_back(group);
          }
          break;
        }
        default: reader.skip(type);
      }
    }
    if (!reader.ok()) return OB_INVALID_DATA;
    if (!flat) {
      return OB_NOT_SUPPORTED;
    }
    if (schema.empty() || schema_fields != static_cast<int32_t>(schema.size()) + 1) {
      return OB_INVALID_DATA;
    }
    for (size_t i = 0; i < schema.size(); ++i) {
      ColumnInfo &column = schema[i].first;
      const int32_t repetition = schema[i].second;
      ColumnInfo info = column;
      info.nullable = repetition != 0;
      switch (info.physical) {
        case PARQUET_BOOLEAN: info.type = Type::boolean; break;
        case PARQUET_INT32:
          if (info.logical_date || 6 == info.converted) info.type = Type::date;
          else if (info.logical_time) info.type = Type::text;
          else info.type = Type::integer;
          break;
        case PARQUET_INT64:
          if (info.logical_timestamp || 9 == info.converted || 10 == info.converted) info.type = Type::datetime;
          else if (info.logical_time) info.type = Type::text;
          else info.type = Type::integer;
          break;
        case PARQUET_INT96: info.type = Type::datetime; break;
        case PARQUET_FLOAT: case PARQUET_DOUBLE: info.type = Type::real; break;
        case PARQUET_BYTE_ARRAY: case PARQUET_FIXED: info.type = Type::text; break;
        default: return OB_NOT_SUPPORTED;
      }
      columns_.push_back(info);
    }
    return OB_SUCCESS;
  }

  int load_metadata()
  {
    int ret = OB_SUCCESS;
    if (loaded_) return OB_SUCCESS;
    std::string head;
    if (OB_FAIL(read_at(0, 4, head)) || memcmp(head.data(), "PAR1", 4) != 0) {
      return OB_INVALID_DATA;
    }
    std::vector<std::pair<ColumnInfo, int32_t>> parsed;
    int32_t repetitions = 0;
    if (OB_FAIL(parse_footer(parsed, repetitions))) return ret;
    loaded_ = true;
    return OB_SUCCESS;
  }
};

ParquetReader::ParquetReader() : impl_(new (std::nothrow) Impl()) {}
ParquetReader::~ParquetReader() { close(); delete impl_; impl_ = nullptr; }

void ParquetReader::close()
{
  if (nullptr != impl_) {
    impl_->columns_.clear();
    impl_->groups_.clear();
    impl_->cursors_.clear();
    impl_->fd_ = -1;
    impl_->begun_ = false;
    impl_->loaded_ = false;
  }
}

int ParquetReader::open(int fd, int64_t file_size, const std::function<bool()> &keep_running)
{
  if (nullptr == impl_) return OB_ALLOCATE_MEMORY_FAILED;
  if (fd < 0 || file_size < 12) return OB_INVALID_DATA;
  impl_->fd_ = fd;
  impl_->file_size_ = file_size;
  impl_->keep_running_ = keep_running;
  return OB_SUCCESS;
}

int ParquetReader::infer(Schema &schema)
{
  int ret = OB_SUCCESS;
  if (nullptr == impl_) return OB_ALLOCATE_MEMORY_FAILED;
  if (OB_FAIL(impl_->load_metadata())) return ret;
  schema.clear();
  for (size_t i = 0; i < impl_->columns_.size(); ++i) {
    const ColumnInfo &info = impl_->columns_[i];
    Column column;
    column.name = info.name;
    column.type = info.type;
    column.nullable = info.nullable;
    column.max_bytes = Type::text == info.type ? 65535 : 0;
    column.source_name = info.name;
    schema.push_back(std::move(column));
  }
  return OB_SUCCESS;
}

int ParquetReader::begin()
{
  if (nullptr == impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = impl_->load_metadata();
  if (OB_FAIL(ret)) return ret;
  impl_->group_index_ = 0;
  impl_->row_in_group_ = 0;
  impl_->begun_ = true;
  impl_->cursors_.clear();
  return OB_SUCCESS;
}

int ParquetReader::next(Row &row)
{
  int ret = OB_SUCCESS;
  if (nullptr == impl_) return OB_ALLOCATE_MEMORY_FAILED;
  if (!impl_->begun_) return OB_NOT_INIT;
  for (;;) {
    if (impl_->group_index_ >= impl_->groups_.size()) return OB_ITER_END;
    RowGroup &group = impl_->groups_[impl_->group_index_];
    if (impl_->row_in_group_ >= group.num_rows) {
      ++impl_->group_index_;
      impl_->row_in_group_ = 0;
      impl_->cursors_.clear();
      continue;
    }
    if (impl_->cursors_.empty()) {
      if (group.chunks.size() != impl_->columns_.size()) return OB_INVALID_DATA;
      impl_->cursors_.resize(group.chunks.size());
      for (size_t i = 0; i < group.chunks.size(); ++i) {
        Cursor &cursor = impl_->cursors_[i];
        cursor.meta = group.chunks[i];
        const ChunkMeta &meta = cursor.meta;
        cursor.page_offset = meta.dict_offset >= 0 && meta.dict_offset < meta.data_offset
                             ? meta.dict_offset : meta.data_offset;
        if (cursor.page_offset < 0) return OB_INVALID_DATA;
        cursor.end_offset = meta.data_offset + meta.compressed_size;
        if (cursor.end_offset <= cursor.page_offset && meta.num_values > 0) return OB_INVALID_DATA;
        if (meta.num_values != group.num_rows) return OB_INVALID_DATA;
      }
    }
    if (impl_->stopped()) return OB_ERR_INTERRUPTED;
    row.assign(impl_->columns_.size(), Field());
    for (size_t i = 0; OB_SUCC(ret) && i < impl_->columns_.size(); ++i) {
      Field field;
      if (OB_FAIL(impl_->value_at(impl_->cursors_[i], impl_->columns_[i],
                                  impl_->row_in_group_, field))) {
      } else {
        row[i] = std::move(field);
      }
    }
    if (OB_SUCC(ret)) ++impl_->row_in_group_;
    return ret;
  }
}
} // namespace oceanbase::sql::file_sql
