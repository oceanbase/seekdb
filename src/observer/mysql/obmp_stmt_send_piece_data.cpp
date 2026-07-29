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

#define USING_LOG_PREFIX SERVER

#include "observer/mysql/obmp_stmt_send_piece_data.h"

namespace oceanbase
{

using namespace rpc;
using namespace common;
using namespace share;
using namespace obmysql;
using namespace sql;

namespace observer
{

int64_t ObPieceBuffer::to_string(char *buffer, int64_t len) const
{
  int64_t pos = 0;
  databuff_printf(buffer, len, pos,
                  "piece_mode:%d",
                  //"buf:%.*s",
                  mode_//, 
                  /*buffer_->length(), buffer_->ptr()*/);
  return pos;
}


int ObPiece::piece_init(ObSQLSessionInfo &session, 
                        int32_t stmt_id, 
                        uint16_t param_id) {
  int ret = OB_SUCCESS;
  lib::ContextParam param;
  ObPieceCache* piece_cache = nullptr;
  set_stmt_id(stmt_id);
  set_param_id(param_id);
  param.set_page_size(OB_MALLOC_NORMAL_BLOCK_SIZE)
      .set_mem_attr("SendPieceProto", ObCtxIds::DEFAULT_CTX_ID);
  if (OB_ISNULL(piece_cache = session.get_piece_cache())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("piece cache is null", K(ret));
  } else if (OB_FAIL(piece_cache->mem_context_->CREATE_CONTEXT(entity_, param))) {
    LOG_WARN("failed to create piece memory context", K(ret));
  } else if (OB_ISNULL(entity_)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc piece memory context", K(ret));
  } else {
    void *buf = nullptr;
    ObPieceBufferArray *buf_array = nullptr;
    ObIAllocator *alloc = &entity_->get_arena_allocator();
    OV (OB_NOT_NULL(buf = alloc->alloc(sizeof(ObPieceBufferArray))),
        OB_ALLOCATE_MEMORY_FAILED, sizeof(ObPieceBufferArray));
    OX (MEMSET(buf, 0, sizeof(ObPieceBufferArray)));
    OV (OB_NOT_NULL(buf_array = new (buf) ObPieceBufferArray(alloc)));
    OZ (buf_array->reserve(OB_MAX_PIECE_BUFFER_COUNT));
    if (OB_SUCC(ret)) {
      set_buffer_array(buf_array);
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("alloc buffer array fail.", K(ret), K(stmt_id), K(param_id));
    }
  }
  LOG_DEBUG("piece init.", K(ret), K(stmt_id), K(param_id));
  // The failure is handed over to the upper layer to release the memory space
  return ret;
}

int ObPieceCache::init_piece_cache(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    if (OB_FAIL(init())) {
      LOG_WARN("piece_cache init fail", K(ret));
    }
  }
  LOG_DEBUG("init piece cache. ");
  return ret;
}

int ObPieceCache::make_piece(int32_t stmt_id, 
                             uint16_t param_id, 
                             ObPiece *&piece, 
                             ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_piece_cache(session))) {
    LOG_WARN("piece_cache init fail", K(ret));
  } else if (NULL == mem_context_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("piece_cache mem_context_ is null", K(ret));
  } else {
    void *buf = NULL;
    OV (OB_NOT_NULL(buf = mem_context_->get_malloc_allocator().alloc(sizeof(ObPiece))),
        OB_ALLOCATE_MEMORY_FAILED, sizeof(ObPiece));
    OX (MEMSET(buf, 0, sizeof(ObPiece)));
    OV (OB_NOT_NULL(piece = new (buf) ObPiece()));
    if (OB_SUCC(ret)) {
      if (OB_FAIL(piece->piece_init(session, stmt_id, param_id))) {
        LOG_WARN("piece init fail.", K(ret), K(stmt_id), K(param_id));
      } else if (OB_FAIL(add_piece(piece))) {
        LOG_WARN("add piece fail.", K(ret), K(stmt_id), K(param_id));
      }
      if (OB_SUCCESS != ret) {
        // clean up memory when failed.
        piece->~ObPiece();
        mem_context_->get_malloc_allocator().free(piece);
        piece = NULL;
      }
    }
  }
  LOG_DEBUG("make piece: ", K(ret), K(stmt_id), K(param_id));
  return ret;
}

int ObPieceCache::add_piece(ObPiece *piece)
{
  int ret = OB_SUCCESS;
  int64_t key = get_piece_key(piece->get_stmt_id(), piece->get_param_id());
  if (OB_INVALID_ID == key) {
    ret = OB_ERR_PARAM_INVALID;
    LOG_WARN("piece key is invalid.", K(ret), K(key));
  } else if (OB_FAIL(piece_map_.set_refactored(key, piece))) {
    LOG_WARN("fail insert ps id to hash map", K(key), K(ret));
  }
  LOG_DEBUG("add piece: ", K(ret), K(key),
            K(piece->get_stmt_id()), K(piece->get_param_id()));
  return ret;
}

int ObPieceCache::remove_piece(int64_t key, ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  ObPiece *piece = NULL;
  if (OB_FAIL(piece_map_.erase_refactored(key, &piece))) {
    LOG_WARN("piece info not exist", K(key), K(ret));
  } else if (OB_ISNULL(piece)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info is null", K(ret));
  } else {
    close_piece(piece, session);
    LOG_DEBUG("remove piece success.", K(key));
  }
  return ret;
}

void ObPieceCache::close_piece(ObPiece *&piece, ObSQLSessionInfo &session)
{
  if (NULL != piece && NULL != mem_context_) {
    LOG_DEBUG("remove piece", K(piece->get_stmt_id()), 
                              K(piece->get_param_id()));
    piece->~ObPiece();
    mem_context_->get_malloc_allocator().free(piece);
    piece = NULL;
  }
}

int ObPieceCache::close_all(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  if (is_inited()) {
    common::ObSEArray<int64_t, 32> piece_keys;
    for (PieceMap::iterator iter = piece_map_.begin();  //ignore ret
        iter != piece_map_.end();
        ++iter) {
      ObPiece *piece = iter->second;
      piece_keys.push_back(get_piece_key(piece->get_stmt_id(), piece->get_param_id()));
    }
    for (int64_t i = 0; i < piece_keys.count(); i++) {
      int64_t key = piece_keys.at(i);
      int64_t tmp_ret = remove_piece(key, session);
      // only save first error ret
      ret = ret == OB_SUCCESS ? tmp_ret : ret;
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("remove piece fail.", K(key), K(tmp_ret));
      }
    }
  }
  return ret;
}

int ObPieceCache::get_piece(int32_t stmt_id, uint16_t param_id, ObPiece *&piece)
{
  int ret = OB_SUCCESS;
  piece = NULL;
  if (!is_inited()) {
    LOG_DEBUG("piece_cache_ is not init.", K(stmt_id), K(param_id));
    // do nothing, do not init piece_cache_ here
  } else {
    if (OB_FAIL(piece_map_.get_refactored(
                            get_piece_key(stmt_id, param_id), piece))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("get piece info failed", K(stmt_id), K(param_id));
      }
    }
  }
  return ret;
}

/*
 * The difference between piece_buffer in fetch and execute:
 * fetch: Character data: each piece has and only one piece_buffer. 
 *                        In this piece_buffer, 
 *                        multiple rows of data are distinguished by piece_mode.
 * execute: character data: with multiple piece_buffer
 */
// for fetch
int ObPieceCache::get_piece_buffer(int32_t stmt_id, 
                                   uint16_t param_id,
                                   int32_t offset,
                                   uint64_t piece_size, 
                                   ObPieceBuffer &piece_buf,
                                   ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  ObPieceBufferArray *buf_array = NULL;
  ObPieceBuffer *old_piece_buf = NULL;
  ObPiece *piece = NULL;
  if (OB_FAIL(get_piece(stmt_id, param_id, piece))) {
    LOG_WARN("get piece fail", K(stmt_id), K(param_id), K(ret) );
  } else if (NULL == piece) {
    ret = OB_ERR_PARAM_INVALID;
    LOG_WARN("piece is null", K(stmt_id), K(ret));
  } else if (NULL == piece->get_buffer_array()
              || 0 == piece->get_buffer_array()->count()) {
    // if piecebuffer is empty, just remove the piece
    if (OB_FAIL(remove_piece(get_piece_key(stmt_id, param_id), session))) {
      LOG_WARN("remove piece fail", K(stmt_id), K(param_id));
    } else {
      // fetch stage, the previous segment read the last data, but the length is exactly equal to piecesize, so the last flag was not set
      piece_buf.set_piece_mode(ObLastPiece);
      piece_buf.set_piece_buffer(NULL);
    }
  } else {
    buf_array = piece->get_buffer_array();
    if (0 == piece_size) {
      // data array
      ret = OB_NOT_SUPPORTED;
      LOG_WARN(" not support array type yet.", K(ret));
    } else if (offset < buf_array->count()) {
      // text
      old_piece_buf = &buf_array->at(offset);
      ObString *buf = old_piece_buf->get_piece_buffer();
      char *&pos = old_piece_buf->get_position();
      int64_t len = piece_size;
      // buf needs to be truncated according to piece_size
      if ((buf->length() - (pos - (buf->ptr()))) <= piece_size) {
        old_piece_buf->set_piece_mode(ObLastPiece);
        piece_buf.set_piece_mode(ObLastPiece);
        len = buf->length() - (pos - (buf->ptr()));
      } else if (ObInvalidPiece == old_piece_buf->get_piece_mode()) {
        old_piece_buf->set_piece_mode(ObFirstPiece);
        piece_buf.set_piece_mode(ObFirstPiece);
      } else if (ObFirstPiece == old_piece_buf->get_piece_mode()) {
        old_piece_buf->set_piece_mode(ObNextPiece);
        piece_buf.set_piece_mode(ObNextPiece);
      } else if (ObNextPiece == old_piece_buf->get_piece_mode()) { 
        piece_buf.set_piece_mode(ObNextPiece);
      }
      piece_buf.get_piece_buffer()->set_length(len);
      piece_buf.get_piece_buffer()->assign_ptr(pos, static_cast<ObString::obstr_size_t>(len));
      pos += len;
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get last piece already.", K(ret), K(offset),
                K(buf_array->count()));
    }
  }
  LOG_DEBUG("get piece buffer.", K(ret), K(stmt_id), K(param_id), 
                                 K(piece_size), K(piece_buf.get_piece_mode()));
  return ret;
}

// for execute
// buf needs to allocate memory in the outer layer ！！！
int ObPieceCache::get_buffer(int32_t stmt_id, 
                             uint16_t param_id, 
                             uint64_t count,
                             uint64_t &length, 
                             common::ObFixedArray<ObSqlString, ObIAllocator> &str_buf,
                             char *is_null_map) {
  int ret = get_mysql_buffer(stmt_id, param_id, length, str_buf.at(0));
  return ret;
}

int ObPieceCache::get_mysql_buffer(int32_t stmt_id,
                                   uint16_t param_id,
                                   uint64_t &length,
                                  ObSqlString &str_buf)
{
  int ret = OB_SUCCESS;
  ObPiece *piece = NULL;
  length = 0;
  str_buf.reset();
  if (OB_FAIL(get_piece(stmt_id, param_id, piece))) {
    LOG_WARN("get piece fail", K(stmt_id), K(param_id), K(ret) );
  } else if (NULL == piece) {
    ret = OB_ERR_PARAM_INVALID;
    LOG_WARN("piece is null", K(stmt_id), K(ret));
  } else {
    ObPieceBufferArray *buffer_array = piece->get_buffer_array();
    for (int64_t i = 0; OB_SUCC(ret) && i < buffer_array->count(); i++) {
      ObPieceBuffer *piece_buffer = &buffer_array->at(i);
      if (NULL != piece_buffer->get_piece_buffer()) {
        const ObString buffer = *(piece_buffer->get_piece_buffer());
        if (OB_FAIL(str_buf.append(buffer))) {
          LOG_WARN("append long data fail.", K(ret));
        } else {}
      }
    }
    length += get_length_length(str_buf.length());
    length += str_buf.length();
  }
  LOG_DEBUG("get buffer.", K(ret), K(stmt_id), K(param_id), K(length));
  return ret;
}

int ObPieceCache::make_piece_buffer(ObIAllocator *allocator,
                                    ObPieceBuffer *&piece_buffer, 
                                    ObPieceMode mode, 
                                    ObString *buf)
{
  // Is this buf supposed to be deep copied, the lifecycle of the outer layer is uncontrollable
  int ret = OB_SUCCESS;
  void *piece_mem = NULL;
  OV (OB_NOT_NULL(piece_mem = allocator->alloc(sizeof(ObPieceBuffer))),
      OB_ALLOCATE_MEMORY_FAILED, sizeof(ObPieceBuffer));
  OX (MEMSET(piece_mem, 0, sizeof(ObPieceBuffer)));
  OV (OB_NOT_NULL(piece_buffer = new (piece_mem) ObPieceBuffer(allocator, mode)));
  CK (OB_NOT_NULL(piece_buffer));
  OZ (piece_buffer->set_piece_buffer(buf));
  LOG_DEBUG("make piece buffer.", K(ret), K(mode), K(buf->length()));
  return ret;
}

ObPieceMode ObPieceCache::get_piece_mode(int8_t mode)
{
  ObPieceMode piece_mode = ObInvalidPiece;
  switch (mode) {
    case 0:
      piece_mode = ObInvalidPiece;
      break;
    case 1:
      piece_mode = ObFirstPiece;
      break;
    case 2:
      piece_mode = ObNextPiece;
      break;
    case 3:
      piece_mode = ObLastPiece;
      break;
    default:
      piece_mode = ObInvalidPiece;
  }
  return piece_mode;
}

int ObPieceCache::add_piece_buffer(ObPiece *piece,
                                   ObPieceMode piece_mode,
                                   ObString *buf)
{
  int ret = OB_SUCCESS;
  ObPieceBuffer *piece_buffer = NULL;
  // Here it is directly pushed in, should the lifecycle of piece_buffer also be considered?
  // The implementation should be changed, do not pass piece_buffer anymore, pass buf and piece_mode
  // Internal call to make_piece_buffer to allocate memory
  if (OB_ISNULL(piece) || OB_ISNULL(piece->get_allocator())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("piece is null.", K(ret));
  } else if (OB_FAIL(make_piece_buffer(piece->get_allocator(), 
                                        piece_buffer, 
                                        piece_mode,
                                        buf))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("piece or piece_buffer is null when add piece buffer", 
              K(ret), K(piece), K(piece_buffer));
  } else if (NULL == piece->get_buffer_array()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("buffer array is null.", K(ret), K(piece->get_stmt_id()), K(piece->get_param_id()));
  } else { /* do nothing */ }
  if (OB_SUCC(ret) && OB_NOT_NULL(piece->get_buffer_array())) {
    ObPieceBufferArray *buffer_array = piece->get_buffer_array();
    if (OB_FAIL(buffer_array->push_back(*piece_buffer))) {
      LOG_WARN("push buffer array fail.", K(ret));
    }
  }
  LOG_DEBUG("add piece buffer.", K(ret), K(piece_mode));
  return ret;
}

static int pre_extend_str(ObPiece *piece,
                           ObSqlString &str,
                           ObPieceBufferArray *buffer_array,
                           const int32_t first_piece_size,
                           const int64_t array_size,
                           bool &is_enable)
{
  int ret = OB_SUCCESS;
  // may need extend str before append if piece size more then 4M
  const int32_t pre_extend_thres = 4194304;
  if (!is_enable) {
  } else if (first_piece_size < pre_extend_thres) {
    // just disable pre extend 
    is_enable = false;
  } else {
    int64_t index_pre = piece->get_position() - 1;
    int64_t total_len = 0;
    do {
      index_pre++;
      if (index_pre < 0 || index_pre >= array_size) {
        break;
      }
      ObPieceBuffer *piece_buffer = &buffer_array->at(index_pre);
      if (NULL != piece_buffer->get_piece_buffer()) {
        const ObString buffer = *(piece_buffer->get_piece_buffer());
        total_len += buffer.length();
      }
    } while (ObLastPiece != buffer_array->at(index_pre).get_piece_mode()
             && ObInvalidPiece != buffer_array->at(index_pre).get_piece_mode());
    ret = str.extend(total_len + 1); // one more bytes for EOF
    is_enable = false;
  }
  return ret;
}

// buf needs to allocate memory in the outer layer ！！！
int ObPieceCache::merge_piece_buffer(ObPiece *piece,
                                     ObSqlString &str)
{
  int ret = OB_SUCCESS;
  ObPieceBufferArray *buffer_array = piece->get_buffer_array();
  if (NULL == buffer_array || 0 == buffer_array->count()) {
    ret = OB_ERR_PARAM_INVALID;
    LOG_WARN("buffer array is null.", K(ret), 
                                      K(piece->get_stmt_id()), 
                                      K(piece->get_param_id()), 
                                      K(buffer_array));
  } else {
    int64_t array_size = buffer_array->count();
    int64_t index = piece->get_position() - 1;
    int64_t len = 0;
    bool enable_pre_extern = true;
    do {
      index++;
      if (index < 0 || index >= array_size) {
        break;
      }
      ObPieceBuffer *piece_buffer = &buffer_array->at(index);
      if (NULL != piece_buffer->get_piece_buffer()) {
        const ObString buffer = *(piece_buffer->get_piece_buffer());
        // reduce alloc/free/memcpy for large buffers
        OZ (pre_extend_str(piece, str, buffer_array, buffer.length(), array_size, enable_pre_extern));
        OZ (str.append(buffer));
        OX (len += buffer.length());
      }
    } while (ObLastPiece != buffer_array->at(index).get_piece_mode()
              && ObInvalidPiece != buffer_array->at(index).get_piece_mode()
              && OB_SUCC(ret));
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (index < 0) {
      piece->set_position(0);
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error index.", K(array_size), K(index));
    } else {
      piece->set_position(index+1);
    }
    
    if (OB_FAIL(ret)) {
      //do nothing.
    } else if (str.length() != len) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("total length is not match total piece length.", K(ret), 
                                                                K(str.length()),
                                                                K(len));
    }
  }
  return ret;
}

} //end of namespace observer
} //end of namespace oceanbase
