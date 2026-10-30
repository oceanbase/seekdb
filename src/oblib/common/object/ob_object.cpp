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

#define USING_LOG_PREFIX COMMON

#include "ob_object.h"
#include "common/ob_tablet_id.h"

using namespace oceanbase;
using namespace oceanbase::common;

bool ObLobId::operator==(const ObLobId &other) const
{
  return tablet_id_ == other.tablet_id_ && lob_id_ == other.lob_id_;
}

bool ObLobId::operator!=(const ObLobId &other) const
{
  return !(operator==(other));
}



void ObLobId::reset()
{
  tablet_id_ = 0;
  lob_id_ = 0;
}




int64_t ObLobDataOutRowCtx::get_real_chunk_size() const
{
  // ObLobDataOutRowCtx::chunk_size_ only have 8 bit, range is 0~255
  // and chunk size can not be zero, so if chunk size is 256KB, ObLobDataOutRowCtx::chunk_size_ is zero
  return chunk_size_ == 0 ? OB_MAX_LOB_CHUNK_SIZE : chunk_size_ * OUTROW_LOB_CHUNK_SIZE_UNIT;
}


bool ObLobData::operator==(const ObLobData &other) const
{
  return id_ == other.id_ && byte_size_ == other.byte_size_;
}


DEF_TO_STRING(ObLobLocatorV2)
{
  int64_t pos = 0;
  uint32_t offset = 0;
  J_OBJ_START();
  J_KV(KP_(ptr), K_(size), K_(has_lob_header));
  J_COMMA();

  if (OB_ISNULL(ptr_)) {
    // do-nothing
  } else if (is_lob_disk_locator() && size_ >= sizeof(ObLobCommon)) {
    ObLobCommon *loc = reinterpret_cast<ObLobCommon *>(ptr_);
    J_KV(K(*loc));
  } else if (has_lob_header_ && size_ >= MEM_LOB_COMMON_HEADER_LEN) {
    ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
    offset += MEM_LOB_COMMON_HEADER_LEN;
    J_KV(K(loc));
    J_COMMA();
    if (is_valid()) {
      if (buf_len > pos) {
        if (loc->has_extern()) {
          ObMemLobExternHeader *extern_header = reinterpret_cast<ObMemLobExternHeader *>(ptr_ + offset);
          offset += MEM_LOB_EXTERN_HEADER_LEN;
          J_KV(K(*extern_header));
          J_COMMA();
          J_KV("extern size", *(uint16_t *)extern_header->data_);
          offset += MEM_LOB_EXTERN_SIZE_LEN;
          J_COMMA();
          if (buf_len > pos && extern_header->flags_.has_tx_info_
              && size_ >= offset + MEM_LOB_EXTERN_TXINFO_LEN) {
            ObMemLobTxInfo *tx_info = reinterpret_cast<ObMemLobTxInfo *>(ptr_ + offset);
            offset += MEM_LOB_EXTERN_TXINFO_LEN;
            J_KV(K(*tx_info));
            J_COMMA();
          }
          if (buf_len > pos && extern_header->flags_.has_location_info_
              && size_ >= offset + MEM_LOB_EXTERN_LOCATIONINFO_LEN) {
            ObMemLobLocationInfo *location_info = reinterpret_cast<ObMemLobLocationInfo *>(ptr_ + offset);
            offset += MEM_LOB_EXTERN_LOCATIONINFO_LEN;
            J_KV(K(*location_info));
            J_COMMA();
          }
          if (buf_len > pos && extern_header->flags_.has_read_snapshot_
              && size_ >= offset + sizeof(ObMemLobReadSnapshot)) {
            ObMemLobReadSnapshot *read_snapshot = reinterpret_cast<ObMemLobReadSnapshot *>(ptr_ + offset);
            offset += sizeof(ObMemLobReadSnapshot) + read_snapshot->size_;
            J_KV(K(*read_snapshot));
            J_COMMA();
          }
          if (buf_len > pos) {
            ObString rowkey_str(MIN(extern_header->rowkey_size_, buf_len - pos), ptr_ + offset);
            offset += extern_header->rowkey_size_;
            J_KV("rowkey", rowkey_str);
            J_COMMA();
          }
          if (buf_len > pos) {
            ObString payload_str(MIN(size_ - offset, buf_len - pos), ptr_ + offset);
            J_KV("disk locator", payload_str);
          }
        } else {
           ObString payload_str(MIN(size_ - offset, buf_len - pos), ptr_ + offset);
           J_KV("inrow data", payload_str);
        }
      }
    } else {
      ObString payload_str(MIN(size_ - offset, buf_len - pos), ptr_ + offset);
      J_KV("inrow", payload_str);
    }
  } else if (has_lob_header_ && size_ < MEM_LOB_COMMON_HEADER_LEN) {
    ObString payload_str(MIN(size_ - offset, buf_len - pos), ptr_ + offset);
    J_KV("content", payload_str);
  } else if (!has_lob_header_) {
    ObString payload_str(MIN(size_ - offset, buf_len - pos), ptr_ + offset);
    J_KV("compatable inrow data", payload_str);
  }

  J_OBJ_END();
  return pos;
}

// Notice: disk_lob_full_size = (disk locator header size if any) + inline buffer
uint32_t ObLobLocatorV2::calc_locator_full_len(const ObMemLobExternFlags &flags,
                                               uint32_t rowkey_size,
                                               uint32_t disk_lob_full_size,
                                               uint32_t read_snapshot_size,
                                               bool is_simple)
{
  uint32_t loc_len = MEM_LOB_COMMON_HEADER_LEN;
  if (!flags.is_empty()) {
    loc_len += MEM_LOB_EXTERN_HEADER_LEN;
    loc_len += MEM_LOB_EXTERN_SIZE_LEN;
    if (flags.has_tx_info_) {
      loc_len += MEM_LOB_EXTERN_TXINFO_LEN;
    }
    if (flags.has_location_info_) {
      loc_len += MEM_LOB_EXTERN_LOCATIONINFO_LEN;
    }
    if (flags.has_read_snapshot_) {
      loc_len += sizeof(ObMemLobReadSnapshot) + read_snapshot_size;
    }
    loc_len += rowkey_size;
  }
  if (is_simple) {
    loc_len += disk_lob_full_size;
  } else {
    loc_len += disk_lob_full_size;
    if (disk_lob_full_size != 0) {
      OB_ASSERT(disk_lob_full_size >= sizeof(ObLobCommon));
    } else {
      loc_len += sizeof(ObLobCommon);
    }
  }
  return loc_len;
}

// fill mem header & rowkey
int ObLobLocatorV2::fill(ObMemLobType type,
                         const ObMemLobExternFlags &flags,
                         const ObString &rowkey_str,
                         const ObLobCommon *disk_loc,
                         uint32_t disk_lob_full_size,
                         uint32_t disk_lob_header_size,
                         uint32_t read_snapshot_size,
                         bool is_simple)
{
  validate_has_lob_header(has_lob_header_);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ptr_)
      || (type <= INVALID_LOB || type >= MAX_LOB_TYPE)
      || size_ < MEM_LOB_COMMON_HEADER_LEN) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: invalid argument", 
      K(ret), K(type), KP(ptr_), K(size_), KP(disk_loc), K(disk_lob_full_size), K(is_simple));
  } else {
    uint32_t offset = 0;
    ObMemLobCommon *loc = new (ptr_ + offset) ObMemLobCommon(type, is_simple);
    loc->lob_common_.is_mem_loc_ = 1;
    loc->set_read_only(false);
    offset += MEM_LOB_COMMON_HEADER_LEN;
    if (flags.is_empty()) {
      // if no extern segment:
      // 1. simple case (only tinytext currently) only ObMemLobCommon and payload without disklocator
      // 2. not simple case:
      //  2.1 payload is empty, disk_lob_full_size should be 0, need to mock a disklocator
      //  2.2 payload is not empty, must has disklocator
      if (is_simple) {
        offset += disk_lob_full_size;
      } else if (disk_lob_full_size != 0) {
        offset += disk_lob_full_size;
        if (disk_lob_full_size < sizeof(ObLobCommon)) {
          ret = OB_INVALID_ARGUMENT;
          COMMON_LOG(WARN, "Lob: invalid buffer size for disk locator",
            K(ret), K(type), K(disk_lob_full_size), K(sizeof(ObLobCommon)));
        }
      } else if (disk_lob_full_size == 0) {
        offset += sizeof(ObLobCommon);
      }
    } else {
      loc->set_extern(true);
      if (OB_UNLIKELY(offset + MEM_LOB_EXTERN_HEADER_LEN + MEM_LOB_EXTERN_SIZE_LEN > size_)) {
        ret = OB_BUF_NOT_ENOUGH;
        COMMON_LOG(WARN, "Lob: invalid buffer size for extern header",
          K(ret), K(type), KP(offset), K(size_));
      } else {
        ObMemLobExternHeader *extern_header =
          new (ptr_ + offset) ObMemLobExternHeader(flags, rowkey_str.length());
        offset += MEM_LOB_EXTERN_HEADER_LEN;

        uint16_t *extern_len = reinterpret_cast<uint16_t *>(ptr_ + offset);
        offset += MEM_LOB_EXTERN_SIZE_LEN;
        *extern_len = 0;

        if (flags.has_tx_info_) {
          offset += MEM_LOB_EXTERN_TXINFO_LEN;
          *extern_len += MEM_LOB_EXTERN_TXINFO_LEN;
        }
        if (flags.has_location_info_) {
          offset += MEM_LOB_EXTERN_LOCATIONINFO_LEN;
          *extern_len += MEM_LOB_EXTERN_LOCATIONINFO_LEN;
        }
        if (flags.has_read_snapshot_) {
          ObMemLobReadSnapshot *read_snapshot = reinterpret_cast<ObMemLobReadSnapshot *>(ptr_ + offset);
          read_snapshot->size_ = read_snapshot_size;
          offset += sizeof(ObMemLobReadSnapshot) + read_snapshot_size;
          *extern_len += sizeof(ObMemLobReadSnapshot) + read_snapshot_size;
        }

        if ((offset + rowkey_str.length()) && OB_UNLIKELY(offset > size_)) {
          ret = OB_BUF_NOT_ENOUGH;
          COMMON_LOG(WARN, "Lob: invalid buffer size for rowkey",
            K(ret), K(type), KP(offset), K(size_), K(flags), K(rowkey_str.length()));
        } else {
          MEMCPY(ptr_ + offset, rowkey_str.ptr(), rowkey_str.length());
          offset += rowkey_str.length();
        }

        if (OB_FAIL(ret)) {
        } else if (disk_lob_full_size == 0) {
          extern_header->payload_offset_ = (offset - MEM_LOB_COMMON_HEADER_LEN - MEM_LOB_EXTERN_HEADER_LEN);
          extern_header->payload_size_ = 0;
        } else if (OB_ISNULL(disk_loc)) {
          ret = OB_INVALID_ARGUMENT;
          COMMON_LOG(WARN, "Lob: building mem-loblocator has externs without disk locator",
            K(ret), K(type), KP(offset), K(size_), K(flags), K(rowkey_str.length()));
        } else if (disk_lob_full_size < sizeof(ObLobCommon)) {
          ret = OB_INVALID_ARGUMENT;
          COMMON_LOG(WARN, "Lob: invalid buffer size for disk locator",
            K(ret), K(type), K(disk_lob_full_size), K(sizeof(ObLobCommon)));
        } else {
          uint32_t disk_loc_header_size = sizeof(ObLobCommon);
          if (disk_loc->in_row_) {
            if (disk_loc->is_init_) {
              disk_loc_header_size += sizeof(ObLobData);
            }
          } else if (disk_lob_header_size != 0) {
            disk_loc_header_size = disk_lob_header_size;
          } else {
            int64_t tbz = disk_loc->get_byte_size(disk_lob_full_size);
            int64_t thz = disk_loc->get_handle_size(tbz);
            disk_loc_header_size = thz;
          }
          if (offset + disk_loc_header_size > size_ || disk_lob_full_size < disk_loc_header_size) {
            ret = OB_INVALID_ARGUMENT;
            COMMON_LOG(WARN, "Lob: invalid disk locator",
              K(ret), K(type), K(offset), K(size_), K(flags), K(disk_lob_header_size), 
              K(disk_loc_header_size), K(disk_lob_full_size), K(*disk_loc));
          } else {
            offset += disk_loc_header_size;
            // offset of disk locator inrow payload
            extern_header->payload_offset_ = (offset - MEM_LOB_COMMON_HEADER_LEN - MEM_LOB_EXTERN_HEADER_LEN);
            extern_header->payload_size_ = disk_lob_full_size - disk_loc_header_size; // size with lob full size
            offset += extern_header->payload_size_;
          }
        }
      }
    }
    if (OB_SUCC(ret) && OB_UNLIKELY(offset > size_)) {
      ret = OB_BUF_NOT_ENOUGH;
      COMMON_LOG(WARN, "Lob: invalid buffer size for disk data",
        K(ret), K(type), KP(offset), K(size_), K(flags), K(disk_lob_full_size));
    }
  }
  return ret;
}


int ObLobLocatorV2:: get_mem_locator(ObMemLobCommon *&mem_loc) const
{
  validate_has_lob_header(has_lob_header_);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!has_lob_header_ || size_ == 0 || OB_ISNULL(ptr_))) {
    ret = OB_ERR_NULL_VALUE;
  } else if (is_lob_disk_locator()) {
    ret = OB_ERR_NULL_VALUE;
  } else {
    mem_loc = reinterpret_cast<ObMemLobCommon*>(ptr_);
  }
  return ret;
}

int ObLobLocatorV2::get_extern_header(ObMemLobExternHeader *&extern_header) const
{
  validate_has_lob_header(has_lob_header_);
  int ret = OB_SUCCESS;
  ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon*>(ptr_);
  if (OB_UNLIKELY(!has_lob_header_ || size_ == 0 || OB_ISNULL(ptr_))) {
    ret = OB_ERR_NULL_VALUE;
  } else if (is_lob_disk_locator()) {
    ret = OB_ERR_NULL_VALUE;
  } else if (!loc->has_extern()) {
    ret = OB_ERR_NULL_VALUE;
  } else {
    extern_header = reinterpret_cast<ObMemLobExternHeader *>(loc->data_);
  }
  return ret;
}

int ObLobLocatorV2::get_rowkey(ObString &rowkey_str) const
{
  validate_has_lob_header(has_lob_header_);
  int ret = OB_SUCCESS;
  ObMemLobExternHeader *extern_header = NULL;
  if (OB_FAIL(get_extern_header(extern_header))) {
  } else {
    uint16_t extern_body_size = *((uint16_t *)(extern_header->data_));
    rowkey_str.assign(extern_header->data_ + MEM_LOB_EXTERN_SIZE_LEN + extern_body_size,
                      extern_header->rowkey_size_);
  }
  return ret;
}

int ObLobLocatorV2::get_disk_locator(ObLobCommon *&disk_loc) const
{
  validate_has_lob_header(has_lob_header_);
  int ret = OB_SUCCESS;
  ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
  if (OB_UNLIKELY(!has_lob_header_ || size_ == 0 || OB_ISNULL(ptr_))) {
    ret = OB_ERR_NULL_VALUE;
    COMMON_LOG(WARN, "Lob: try to get disk locator without lob header", K(ret));
  } else if (is_lob_disk_locator()) {
    disk_loc = reinterpret_cast<ObLobCommon *>(ptr_);
  } else if (loc->is_simple()) {
    ret = OB_ERR_NULL_VALUE;
    COMMON_LOG(WARN, "Lob: simple lob locator does not has disk locator", K(ret));
  } else if (loc->has_extern()) {
    ObString rowkey_str;
    if (OB_FAIL(get_rowkey(rowkey_str))) {
    } else {
      disk_loc = reinterpret_cast<ObLobCommon *>(rowkey_str.ptr() + rowkey_str.length());
    }
  } else { // not simple, no extern
    disk_loc = reinterpret_cast<ObLobCommon *>(loc->data_);
  }
  return ret;
}

int ObLobLocatorV2::get_disk_locator(ObString &disc_loc_buff) const
{
  int ret = OB_SUCCESS;
  ObLobCommon *disk_loc = NULL;
  if (OB_FAIL(get_disk_locator(disk_loc))) {
  } else {
    int64_t handle_size = reinterpret_cast<intptr_t>(disk_loc) - reinterpret_cast<intptr_t>(ptr_);
    if (handle_size > size_) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      if (disk_loc->in_row_) {
        handle_size = size_ - handle_size;
      } else {
        handle_size = disk_loc->get_handle_size(0);
      }
      disc_loc_buff.assign_ptr(reinterpret_cast<const char *>(disk_loc), handle_size);
    }
  }
  return ret;
}

int ObLobLocatorV2::get_inrow_data(ObString &inrow_data) const
{
  int ret =  OB_SUCCESS;
  ObString disk_loc_buff;
  ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
  if (!has_lob_header_ || size_ == 0) {
    inrow_data.assign_ptr(ptr_, size_);
  } else if (OB_ISNULL(ptr_)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Lob: get null ptr", K(ret), K(size_), K(ptr_));
  } else if (is_freed()) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: has been freed", K(ret), KPC(loc));
  } else if (!is_lob_disk_locator() && loc->is_simple()) {
    inrow_data.assign_ptr(ptr_ + MEM_LOB_COMMON_HEADER_LEN, size_ - MEM_LOB_COMMON_HEADER_LEN);
  } else if (OB_FAIL(get_disk_locator(disk_loc_buff))) {
  } else {
    ObLobCommon *disk_loc = reinterpret_cast<ObLobCommon *>(disk_loc_buff.ptr());
    if (disk_loc->in_row_) {
      if (!is_lob_disk_locator() && !loc->has_inrow_data()) { // never disk locator inrow, but mem locator outrow
        ret = OB_INVALID_ARGUMENT;
        COMMON_LOG(WARN, "Lob: invalid outrow data", K(ret));
      } else {
        inrow_data.assign_ptr(disk_loc->get_inrow_data_ptr(), disk_loc->get_byte_size(disk_loc_buff.length()));
      }
    } else if (!is_lob_disk_locator() && has_inrow_data()) {
      if (has_extern()) {
        ObMemLobExternHeader *ext_header = nullptr;
        if (OB_FAIL(get_extern_header(ext_header))) {
        } else if (ext_header->payload_offset_ + ext_header->payload_size_ > size_) {
          ret = OB_INVALID_ARGUMENT;
          COMMON_LOG(WARN, "Lob: invalid payload data", K(ret), K(*ext_header), K(size_));
        } else {
          inrow_data.assign_ptr(ext_header->data_ + ext_header->payload_offset_, ext_header->payload_size_);
        }
      } else { // no extern [mem lob common][lob common][inrow data]
        int64_t handle_offset = reinterpret_cast<intptr_t>(disk_loc) - reinterpret_cast<intptr_t>(ptr_);
        int64_t byte_size = disk_loc->get_byte_size(disk_loc_buff.length());
        int64_t handle_size = disk_loc->get_handle_size(byte_size);
        if (byte_size + handle_size + handle_offset > size_) {
          ret = OB_INVALID_ARGUMENT;
          COMMON_LOG(WARN, "Lob: invalid inrow data", K(ret), K(byte_size), K(handle_size),
                     K(*disk_loc), K(handle_offset), K(size_));
        } else {
          inrow_data.assign_ptr(disk_loc_buff.ptr() + handle_size, byte_size);
        }
      }
    } else { // out row
      ret = OB_ERR_NULL_VALUE;
      COMMON_LOG(WARN, "Lob: Maybe a bug, get inrow data of outrow lob", K(ret), K(lbt()));
    }
  }
  return ret;
}

bool ObLobLocatorV2::is_inrow() const
{
  int ret = OB_SUCCESS;
  bool bret = false;
  ObString disk_loc_buff;
  ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
  if (!has_lob_header_ || size_ == 0 || OB_ISNULL(ptr_)) {
    bret = true;
  } else if (!is_lob_disk_locator() && loc->is_simple()) {
    bret = true;
  } else if (OB_FAIL(get_disk_locator(disk_loc_buff))) {
  } else {
    ObLobCommon *disk_loc = reinterpret_cast<ObLobCommon *>(disk_loc_buff.ptr());
    bret = disk_loc->in_row_;
  }
  return bret;
}

bool ObLobLocatorV2::is_empty_lob() const
{
  int ret =  OB_SUCCESS;
  bool bret = false;
  ObString disk_loc_buff;
  ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
  if (!has_lob_header_ || size_ == 0 || OB_ISNULL(ptr_)) {
    bret = (size_ == 0);
  } else if (!is_lob_disk_locator() && loc->is_simple()) {
    bret = (size_ - MEM_LOB_COMMON_HEADER_LEN == 0);
  } else if (OB_FAIL(get_disk_locator(disk_loc_buff))) {
  } else {
    ObLobCommon *disk_loc = reinterpret_cast<ObLobCommon *>(disk_loc_buff.ptr());
    if (disk_loc->in_row_) {
      bret = (disk_loc->get_byte_size(disk_loc_buff.length()) == 0);
    }
  }
  return (ret == OB_SUCCESS ? bret : false);
}

int ObLobLocatorV2::get_lob_data_byte_len(int64_t &len) const
{
  int ret =  OB_SUCCESS;
  ObString disk_loc_buff;
  ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
  if (!has_lob_header_ || size_ == 0 || OB_ISNULL(ptr_)) {
    len = size_;
  } else if (!is_lob_disk_locator() && loc->is_simple()) {
    len = size_ - MEM_LOB_COMMON_HEADER_LEN;
  } else if (OB_FAIL(get_disk_locator(disk_loc_buff))) {
  } else {
    ObLobCommon *disk_loc = reinterpret_cast<ObLobCommon *>(disk_loc_buff.ptr());
    len = disk_loc->get_byte_size(disk_loc_buff.length());
  }
  return ret;
}


int ObLobLocatorV2::get_tx_info(ObMemLobTxInfo *&tx_info) const
{
  int ret =  OB_SUCCESS;
  ObMemLobExternHeader *extern_header = NULL;
  if (OB_SUCC(get_extern_header(extern_header))) {
    if (extern_header->flags_.has_tx_info_) {
      tx_info = reinterpret_cast<ObMemLobTxInfo *>(extern_header->data_ + MEM_LOB_EXTERN_SIZE_LEN);
    } else {
      ret = OB_ERR_NULL_VALUE;
      COMMON_LOG(WARN, "Lob: does not have tx info", K(this), K(ret));
    }
  }
  return ret;
}

int ObLobLocatorV2::get_location_info(ObMemLobLocationInfo *&location_info) const
{
  int ret =  OB_SUCCESS;
  ObMemLobExternHeader *extern_header = NULL;
  if (OB_SUCC(get_extern_header(extern_header))) {
    char *cur_pos = extern_header->data_ + MEM_LOB_EXTERN_SIZE_LEN;
    if (extern_header->flags_.has_tx_info_) {
      cur_pos += MEM_LOB_EXTERN_TXINFO_LEN;
    }
    if (extern_header->flags_.has_location_info_) {
      location_info = reinterpret_cast<ObMemLobLocationInfo *>(cur_pos);
    } else {
      ret = OB_ERR_NULL_VALUE;
      COMMON_LOG(WARN, "Lob: does not have location info", K(this), K(ret));
    }
  }
  return ret;
}

int ObLobLocatorV2::get_read_snapshot_data(ObString &read_snapshot_data) const
{
  int ret =  OB_SUCCESS;
  ObMemLobExternHeader *extern_header = NULL;
  if (OB_SUCC(get_extern_header(extern_header))) {
    char *cur_pos = extern_header->data_ + MEM_LOB_EXTERN_SIZE_LEN;
    if (extern_header->flags_.has_tx_info_) {
      cur_pos += MEM_LOB_EXTERN_TXINFO_LEN;
    }
    if (extern_header->flags_.has_location_info_) {
      cur_pos += MEM_LOB_EXTERN_LOCATIONINFO_LEN;
    }
    
    if (extern_header->flags_.has_read_snapshot_) {
      ObMemLobReadSnapshot *read_snapshot = reinterpret_cast<ObMemLobReadSnapshot *>(cur_pos);
      if (read_snapshot->size_ > 0) {
        read_snapshot_data.assign_ptr(read_snapshot->data_, read_snapshot->size_);
      }
    } else {
      ret = OB_ERR_NULL_VALUE;
      COMMON_LOG(WARN, "Lob: does not have tx read snapshot", K(this), K(ret));
    }
  }
  return ret;
}

int ObLobLocatorV2::get_real_locator_len(int64_t &real_len) const
{
  int ret = OB_SUCCESS;
  ObLobCommon *disk_loc = NULL;
  ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
  real_len = size_;
  if (!has_lob_header_ || size_ == 0 || OB_ISNULL(ptr_)) {
  } else if (!is_lob_disk_locator() && loc->is_simple_) {
  } else if (OB_FAIL(get_disk_locator(disk_loc))) {
  } else {
    real_len = (uintptr_t)disk_loc - (uintptr_t)ptr_;
    if (disk_loc->in_row_) {
      real_len += sizeof(ObLobCommon);
      if (disk_loc->is_init_) {
        real_len += sizeof(ObLobData);
      }
    } else {
      real_len += disk_loc->get_handle_size(0);
    }
  }
  return ret;
}

int ObLobLocatorV2::get_chunk_size(int64_t &chunk_size) const
{
  int ret = OB_SUCCESS;
  ObLobCommon *disk_loc = nullptr;
  if (! has_lob_header_ || size_ == 0 || OB_ISNULL(ptr_)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "no locator or is null", K(ret), K(has_lob_header_), K(size_), KP(ptr_));
  } else if (! is_persist_lob()) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "non-persist_lob should not call this function", K(ret), KPC(this));
  } else if (is_inrow()) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "inrow-persist_lob should not call this function", K(ret), KPC(this));
  } else if (OB_FAIL(get_disk_locator(disk_loc))) {
  } else if(((uintptr_t)disk_loc - (uintptr_t)ptr_) < DISK_LOB_OUTROW_FULL_SIZE) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "size overflow", K(ret), KPC(this), KP(disk_loc), KP(ptr_), "left_size", ((uintptr_t)disk_loc - (uintptr_t)ptr_));
  } else if (! disk_loc ->is_init_) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "disk lob not init", K(ret), KPC(this), KPC(disk_loc));
  } else {
    const ObLobData *lob_data = reinterpret_cast<const ObLobData*>(disk_loc->buffer_);
    const ObLobDataOutRowCtx *ctx = reinterpret_cast<const ObLobDataOutRowCtx*>(lob_data->buffer_);
    chunk_size = ctx->get_real_chunk_size();
  }
  return ret;
}

// Notice: this payload is payload with disk locator if it exist
int ObLobLocatorV2::set_payload_data(const ObString& payload)
{
  OB_ASSERT(has_lob_header_); // only used in build_lob_locator_v2, must has lob header
  int ret = OB_SUCCESS;
  ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
  if (size_ == 0 || OB_ISNULL(ptr_)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Lob: get null ptr", K(ret), K(size_), K(ptr_));
  } else if (loc->lob_common_.is_mem_loc_ != 1) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Lob: get disk loc", K(ret), K(loc->lob_common_));
  } else {
    uint32_t buf_len = size_ - (loc->data_ - ptr_);
    if (!loc->has_extern()) {
      OB_ASSERT(payload.length() == buf_len); // debug
      MEMCPY(loc->data_, payload.ptr(), buf_len);
    } else {
      ObString disk_loc_buff;
      if (OB_SUCC(get_disk_locator(disk_loc_buff))) {
        buf_len = size_ - (disk_loc_buff.ptr() - ptr_);
        OB_ASSERT(payload.length() == buf_len);
        MEMCPY(disk_loc_buff.ptr(), payload.ptr(), payload.length());
      }
    }
  }
  return ret;
}

int ObLobLocatorV2::set_payload_data(const ObLobCommon *lob_comm, const ObString& payload)
{
  OB_ASSERT(has_lob_header_); // only used for fill temp lob header or default value lob locater v2
  int ret = OB_SUCCESS;
  uint32_t buf_len = 0;
  ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
  if (size_ == 0 || OB_ISNULL(ptr_)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Lob: get null ptr", K(ret), K(size_), K(ptr_));
  } else if (loc->lob_common_.is_mem_loc_ != 1) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Lob: get disk loc", K(ret), K(loc->lob_common_));
  } else if (loc->is_simple()) {
    buf_len = size_ - (loc->data_ - ptr_);
    OB_ASSERT(payload.length() <= buf_len);
    if (payload.length() > 0) {
      MEMCPY(loc->data_, payload.ptr(), buf_len);
    }
  } else {
    char *buf = NULL;
    ObString disk_loc_buff;
    if (loc->has_extern()) {
      if (OB_SUCC(get_disk_locator(disk_loc_buff))) {
        buf = disk_loc_buff.ptr();
        buf_len = (size_ - (disk_loc_buff.ptr() - ptr_));
      }
    } else if (!loc->has_extern()) {
      buf = loc->data_;
      buf_len = size_ - (loc->data_ - ptr_);
    }
    if (OB_SUCC(ret)) {
      uint32 disk_lob_header_len = sizeof(ObLobCommon);
      if (lob_comm->in_row_) {
        disk_lob_header_len += lob_comm->is_init_ ? sizeof(ObLobData) : 0;
      } else {
        disk_lob_header_len = lob_comm->get_handle_size(0);
      }
      OB_ASSERT(payload.length() + disk_lob_header_len <= buf_len);
      MEMCPY(buf, lob_comm, disk_lob_header_len);
      if (payload.length() > 0) {
        MEMCPY(buf + disk_lob_header_len, payload.ptr(), payload.length());
      }
    }
  }
  return ret;
}

int ObLobLocatorV2::set_table_info(const uint64_t &table_id, const uint32_t &column_idx)
{
  validate_has_lob_header(has_lob_header_);
  int ret = OB_SUCCESS;
  ObMemLobExternHeader *extern_header = NULL;
  if (OB_SUCC(get_extern_header(extern_header))) {
    extern_header->table_id_ = table_id;
    extern_header->column_idx_ = column_idx;
  }
  return ret;
}

int ObLobLocatorV2::set_tx_info(const ObMemLobTxInfo &tx_info)
{
  validate_has_lob_header(has_lob_header_);
  int ret = OB_SUCCESS;
  ObMemLobTxInfo *tx_info_ptr = NULL;
  if (OB_SUCC(get_tx_info(tx_info_ptr))) {
    *tx_info_ptr = tx_info;
  }
  return ret;
}

int ObLobLocatorV2::set_location_info(const ObMemLobLocationInfo &location_info)
{
  validate_has_lob_header(has_lob_header_);
  int ret = OB_SUCCESS;
  ObMemLobLocationInfo *loc_info_ptr = NULL;
  if (OB_SUCC(get_location_info(loc_info_ptr))) {
    *loc_info_ptr = location_info;
  }
  return ret;
}


OB_DEF_SERIALIZE(ObLobLocatorV2)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (NULL == buf || pos < 0 || pos > buf_len) {
    ret = OB_INVALID_ARGUMENT;
  } else if (new_pos + size_ + sizeof(size_) + sizeof(has_lob_header_) > buf_len) {
    ret = OB_SERIALIZE_ERROR;
  } else {
    *reinterpret_cast<uint32_t*>(buf + new_pos) = size_;
    new_pos += sizeof(size_);
    *reinterpret_cast<bool*>(buf + new_pos) = has_lob_header_;
    new_pos += sizeof(has_lob_header_);
    MEMCPY(buf + new_pos, ptr_, size_);
    new_pos += size_;
  }
  if (OB_SUCC(ret)) {
    pos = new_pos;
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObLobLocatorV2)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (NULL == buf || pos < 0 || pos > data_len) {
    ret = OB_INVALID_ARGUMENT;
  } else if (new_pos + size_ + sizeof(size_) + sizeof(has_lob_header_) > data_len) {
    ret = OB_DESERIALIZE_ERROR;
  } else {
    size_ = *reinterpret_cast<const uint32_t*>(buf + new_pos);
    new_pos += sizeof(size_);
    has_lob_header_ = *reinterpret_cast<const uint32_t*>(buf + new_pos);
    new_pos += sizeof(has_lob_header_);
    ptr_ = const_cast<char*>(buf + new_pos);
    new_pos += size_;
  }
  if (OB_SUCC(ret)) {
    pos = new_pos;
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObLobLocatorV2)
{
  return size_ + sizeof(size_) + sizeof(has_lob_header_);
}

ObDocId::ObDocId()
  : tablet_id_(ObTabletID::INVALID_TABLET_ID),
    seq_id_(0)
{
  static_assert(sizeof(ObDocId) == OB_DOC_ID_COLUMN_BYTE_LENGTH, "size of ObDocId isn't equal to OB_DOC_ID_COLUMN_BYTE_LENGTH");
}

ObDocId::ObDocId(const uint64_t tablet_id, const uint64_t seq_id)
  : tablet_id_(tablet_id),
    seq_id_(seq_id)
{
  static_assert(sizeof(ObDocId) == OB_DOC_ID_COLUMN_BYTE_LENGTH, "size of ObDocId isn't equal to OB_DOC_ID_COLUMN_BYTE_LENGTH");
}

bool ObDocId::operator==(const ObDocId &other) const
{
  return tablet_id_ == other.tablet_id_ && seq_id_ == other.seq_id_;
}

bool ObDocId::operator!=(const ObDocId &other) const
{
  return !(operator==(other));
}



void ObDocId::reset()
{
  tablet_id_ = ObTabletID::INVALID_TABLET_ID;
  seq_id_ = 0;
}

bool ObDocId::is_valid() const
{
  return ObTabletID(tablet_id_).is_valid() && seq_id_ > 0;
}

int ObDocId::hash(uint64_t &hash_val) const
{
  hash_val = murmurhash(&tablet_id_, sizeof(seq_id_), OB_DOC_ID_HASH);
  hash_val = murmurhash(&seq_id_, sizeof(seq_id_), hash_val);
  return common::OB_SUCCESS;
}

ObString ObDocId::get_string() const
{
  return ObString(OB_DOC_ID_COLUMN_BYTE_LENGTH, reinterpret_cast<const char *>(this));
}

int ObDocId::from_string(const ObString &doc_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(doc_id.ptr()) || OB_UNLIKELY(doc_id.length() < OB_DOC_ID_COLUMN_BYTE_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid document id", K(ret));
  } else {
    const ObDocId *doc_id_ptr = reinterpret_cast<const ObDocId *>(doc_id.ptr());
    tablet_id_ = doc_id_ptr->tablet_id_;
    seq_id_ = doc_id_ptr->seq_id_;
  }
  return ret;
}

ObCenterId::ObCenterId()
  : tablet_id_(ObTabletID::INVALID_TABLET_ID),
    center_id_(-1)
{
  static_assert(sizeof(ObCenterId) == OB_DOC_ID_COLUMN_BYTE_LENGTH, "size of ObCenterId isn't equal to OB_DOC_ID_COLUMN_BYTE_LENGTH");
}

ObCenterId::ObCenterId(const uint64_t tablet_id, const uint64_t center_id)
  : tablet_id_(tablet_id),
    center_id_(center_id)
{
  static_assert(sizeof(ObCenterId) == OB_DOC_ID_COLUMN_BYTE_LENGTH, "size of ObCenterId isn't equal to OB_DOC_ID_COLUMN_BYTE_LENGTH");
}






bool ObCenterId::is_valid() const
{
  return ObTabletID(tablet_id_).is_valid() && center_id_ >= 0;
}

ObPqCenterId::ObPqCenterId()
  : tablet_id_(ObTabletID::INVALID_TABLET_ID),
    m_id_(0),
    center_id_(-1)
{
  static_assert(sizeof(ObPqCenterId) == OB_DOC_ID_COLUMN_BYTE_LENGTH, "size of ObPqCenterId isn't equal to OB_DOC_ID_COLUMN_BYTE_LENGTH");
}

ObPqCenterId::ObPqCenterId(const uint64_t tablet_id, const uint32_t m_id, const uint32_t center_id)
  : tablet_id_(tablet_id),
    m_id_(m_id),
    center_id_(center_id)
{
  static_assert(sizeof(ObPqCenterId) == OB_DOC_ID_COLUMN_BYTE_LENGTH, "size of ObPqCenterId isn't equal to OB_DOC_ID_COLUMN_BYTE_LENGTH");
}






bool ObPqCenterId::is_valid() const
{
  return ObTabletID(tablet_id_).is_valid() && center_id_ >= 0 && m_id_ > 0;
}
