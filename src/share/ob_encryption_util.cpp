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
#include "share/ob_encryption_util.h"
#include <cstddef>
#include <limits>
#include "lib/allocator/page_arena.h"
#include "rust/crypto.h"

namespace oceanbase
{
using namespace common;
namespace share
{

namespace
{
bool valid_buffer(const char *data, const int64_t size)
{
  return size >= 0 && static_cast<uint64_t>(size) <=
      static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) &&
      (size == 0 || data != nullptr);
}

bool overlaps(const char *left, const int64_t left_size,
              const char *right, const int64_t right_size)
{
  const uintptr_t left_addr = reinterpret_cast<uintptr_t>(left);
  const uintptr_t right_addr = reinterpret_cast<uintptr_t>(right);
  return left_size > 0 && right_size > 0 &&
      (left_addr <= right_addr ? right_addr - left_addr < static_cast<uint64_t>(left_size)
                               : left_addr - right_addr < static_cast<uint64_t>(right_size));
}

int borrow_input(const char *data, const int64_t size,
                 char *output, const int64_t output_size, char *tag, const int64_t tag_size,
                 ObIAllocator &allocator, rust::Slice<const uint8_t> &input)
{
  int ret = OB_SUCCESS;
  if (overlaps(data, size, output, output_size) || overlaps(data, size, tag, tag_size)) {
    // OpenSSL permits in-place buffers, but Rust slices must not alias mutable output.
    char *copy = static_cast<char *>(allocator.alloc(size));
    if (OB_ISNULL(copy)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(copy, data, size);
      input = rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(copy), size);
    }
  } else {
    input = rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(data), size);
  }
  return ret;
}

int cipher_error(const crypto::OperationResult &result, const int backend_error)
{
  int ret = OB_SUCCESS;
  switch (result.code) {
    case crypto::Code::Ok:
      break;
    case crypto::Code::InvalidArgument:
    case crypto::Code::Unsupported:
      ret = OB_INVALID_ARGUMENT;
      break;
    case crypto::Code::Allocation:
      ret = OB_ALLOCATE_MEMORY_FAILED;
      break;
    default:
      ret = backend_error;
      LOG_WARN("cipher operation failed", K(ret), K(result.backend_code));
      break;
  }
  return ret;
}
} // namespace

int ObBlockCipher::get_key_length(const ObCipherOpMode opmode)
{
  const crypto::CipherInfo info = crypto::cipher_info(static_cast<uint32_t>(opmode));
  return info.result.code == crypto::Code::Ok ? static_cast<int>(info.key_len) : 0;
}

bool ObBlockCipher::is_need_iv(const ObCipherOpMode opmode)
{
  return get_iv_length(opmode) != 0;
}

bool ObBlockCipher::is_need_aead(const ObCipherOpMode opmode)
{
  const crypto::CipherInfo info = crypto::cipher_info(static_cast<uint32_t>(opmode));
  return info.result.code == crypto::Code::Ok && info.aead;
}

bool ObBlockCipher::is_need_padding(const ObCipherOpMode opmode)
{
  const crypto::CipherInfo info = crypto::cipher_info(static_cast<uint32_t>(opmode));
  return info.result.code == crypto::Code::Ok && info.padding;
}

int64_t ObBlockCipher::get_iv_length(const ObCipherOpMode opmode)
{
  const crypto::CipherInfo info = crypto::cipher_info(static_cast<uint32_t>(opmode));
  return info.result.code == crypto::Code::Ok ? static_cast<int64_t>(info.iv_len) : 0;
}

int64_t ObBlockCipher::get_ciphertext_length(const ObCipherOpMode opmode, const int64_t data_len)
{
  int64_t size = data_len;
  if (is_need_padding(opmode)) {
    size = -1;
    if (data_len >= 0 && static_cast<uint64_t>(data_len) <= std::numeric_limits<size_t>::max()) {
      const crypto::OperationResult result =
          crypto::ciphertext_size(static_cast<uint32_t>(opmode), static_cast<size_t>(data_len));
      if (result.code == crypto::Code::Ok && result.size <= static_cast<uint64_t>(INT64_MAX)) {
        size = static_cast<int64_t>(result.size);
      }
    }
  }
  return size;
}

bool ObBlockCipher::is_valid_cipher_opmode(const ObCipherOpMode opmode)
{
  return opmode > ObCipherOpMode::ob_invalid_mode && opmode < ObCipherOpMode::ob_max_mode &&
         opmode != ObCipherOpMode::ob_sm4_mode;
}

int ObBlockCipher::encrypt(const char *key, const int64_t key_len,
                           const char *data, const int64_t data_len, const int64_t buf_len,
                           const char *iv, const int64_t iv_len,
                           const char *aad, const int64_t aad_len, const int64_t tag_len,
                           enum ObCipherOpMode mode, char *buf, int64_t &out_len, char *tag)
{
  int ret = OB_SUCCESS;
  const crypto::CipherInfo info = crypto::cipher_info(static_cast<uint32_t>(mode));
  const int64_t iv_size = info.iv_len == 0 ? 0 : iv_len;
  const int64_t aad_size = info.aead ? aad_len : 0;
  const int64_t tag_size = info.aead ? tag_len : 0;
  if (!is_valid_cipher_opmode(mode) ||
      !valid_buffer(key, key_len) || !valid_buffer(data, data_len) ||
      OB_ISNULL(buf) || !valid_buffer(buf, buf_len) ||
      !valid_buffer(iv, iv_size) || !valid_buffer(aad, aad_size) ||
      !valid_buffer(tag, tag_size) || overlaps(buf, buf_len, tag, tag_size)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(cipher_error(info.result, OB_ERR_AES_ENCRYPT))) {
  } else {
    ObArenaAllocator allocator("CryptoInput");
    rust::Slice<const uint8_t> key_input, iv_input, aad_input, data_input;
    if (OB_FAIL(borrow_input(key, key_len, buf, buf_len, tag, tag_size, allocator, key_input))) {
    } else if (OB_FAIL(borrow_input(iv, iv_size, buf, buf_len, tag, tag_size, allocator, iv_input))) {
    } else if (OB_FAIL(borrow_input(aad, aad_size, buf, buf_len, tag, tag_size, allocator, aad_input))) {
    } else if (OB_FAIL(borrow_input(data, data_len, buf, buf_len, tag, tag_size, allocator, data_input))) {
    } else {
      const crypto::OperationResult result = crypto::encrypt_into(static_cast<uint32_t>(mode),
          key_input, iv_input, aad_input, data_input,
          rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(buf), buf_len),
          rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(tag), tag_size));
      if (OB_FAIL(cipher_error(result, OB_ERR_AES_ENCRYPT))) {
      } else {
        out_len = static_cast<int64_t>(result.size);
      }
    }
  }
  return ret;
}

int ObBlockCipher::decrypt(const char *key, const int64_t key_len,
                           const char *data, const int64_t data_len, const int64_t buf_len,
                           const char *iv, const int64_t iv_len, const char *aad,
                           const int64_t aad_len, const char *tag, const int64_t tag_len,
                           enum ObCipherOpMode mode, char *buf, int64_t &out_len)
{
  int ret = OB_SUCCESS;
  const crypto::CipherInfo info = crypto::cipher_info(static_cast<uint32_t>(mode));
  const int64_t iv_size = info.iv_len == 0 ? 0 : iv_len;
  const int64_t aad_size = info.aead ? aad_len : 0;
  const int64_t tag_size = info.aead ? tag_len : 0;
  if (!is_valid_cipher_opmode(mode) ||
      !valid_buffer(key, key_len) || !valid_buffer(data, data_len) ||
      OB_ISNULL(buf) || !valid_buffer(buf, buf_len) ||
      !valid_buffer(iv, iv_size) || !valid_buffer(aad, aad_size) || !valid_buffer(tag, tag_size)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(cipher_error(info.result, OB_ERR_AES_DECRYPT))) {
  } else {
    ObArenaAllocator allocator("CryptoInput");
    rust::Slice<const uint8_t> key_input, iv_input, aad_input, data_input, tag_input;
    if (OB_FAIL(borrow_input(key, key_len, buf, buf_len, nullptr, 0, allocator, key_input))) {
    } else if (OB_FAIL(borrow_input(iv, iv_size, buf, buf_len, nullptr, 0, allocator, iv_input))) {
    } else if (OB_FAIL(borrow_input(aad, aad_size, buf, buf_len, nullptr, 0, allocator, aad_input))) {
    } else if (OB_FAIL(borrow_input(data, data_len, buf, buf_len, nullptr, 0, allocator, data_input))) {
    } else if (OB_FAIL(borrow_input(tag, tag_size, buf, buf_len, nullptr, 0, allocator, tag_input))) {
    } else {
      const crypto::OperationResult result = crypto::decrypt_into(static_cast<uint32_t>(mode),
          key_input, iv_input, aad_input, data_input, tag_input,
          rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(buf), buf_len));
      if (OB_FAIL(cipher_error(result, OB_ERR_AES_DECRYPT))) {
      } else {
        out_len = static_cast<int64_t>(result.size);
      }
    }
  }
  return ret;
}

bool ObEncryptionUtil::is_aes_encryption(const ObCipherOpMode opmode) {
  return (opmode >= ObCipherOpMode::ob_aes_128_ecb && opmode <= ObCipherOpMode::ob_aes_256_ofb) ||
         (opmode >= ObCipherOpMode::ob_aes_128_gcm && opmode <= ObCipherOpMode::ob_aes_256_gcm);
}

bool ObEncryptionUtil::is_sm4_encryption(const ObCipherOpMode opmode) {
  return (opmode >= ObCipherOpMode::ob_sm4_mode && opmode <= ObCipherOpMode::ob_sm4_cbc_mode) ||
         (opmode >= ObCipherOpMode::ob_sm4_cbc && opmode <= ObCipherOpMode::ob_sm4_gcm);
}

bool ObEncryptionUtil::is_ecb_mode(const ObCipherOpMode opmode)
{
  return (opmode >= ObCipherOpMode::ob_aes_128_ecb && opmode <= ob_aes_256_ecb) ||
         (opmode == ObCipherOpMode::ob_sm4_ecb);
}

int ObHashUtil::hash(const enum ObHashAlgorithm algo, const ObString data,
                     ObIAllocator &allocator, ObString &output)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  const char *data_ptr = NULL != data.ptr() ? data.ptr() : "";
  int64_t buf_len = 0;
  int64_t out_len = 0;
  if (OB_FAIL(get_hash_output_len(algo, buf_len))) {
  } else if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(hash(algo, data_ptr, data.length(), buf, buf_len, out_len))) {
  } else {
    output.assign_ptr(buf, static_cast<int32_t>(out_len));
  }
  return ret;
}

int ObHashUtil::hash(const enum ObHashAlgorithm algo, const char *data, const int64_t data_len,
                       char *buf, const int64_t &buf_len, int64_t &out_len)
{
  int ret = OB_SUCCESS;
  int64_t expect_out_len = 0;
  if (ObHashAlgorithm::OB_HASH_INVALID == algo || OB_ISNULL(data) ||
      !valid_buffer(data, data_len) || OB_ISNULL(buf) || !valid_buffer(buf, buf_len)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_hash_output_len(algo, expect_out_len))) {
  } else if (OB_UNLIKELY(buf_len < expect_out_len)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObArenaAllocator allocator("CryptoInput");
    rust::Slice<const uint8_t> input;
    if (OB_FAIL(borrow_input(data, data_len, buf, buf_len, nullptr, 0, allocator, input))) {
    } else {
      const crypto::OperationResult result = crypto::digest_into(static_cast<uint32_t>(algo), input,
          rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(buf), buf_len));
      if (result.code == crypto::Code::Allocation) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (result.code != crypto::Code::Ok) {
        ret = OB_ERR_AES_ENCRYPT;
        LOG_WARN("hash operation failed", K(ret), K(result.backend_code));
      } else if (OB_UNLIKELY(result.size != static_cast<size_t>(expect_out_len))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        out_len = static_cast<int64_t>(result.size);
      }
    }
  }
  return ret;
}

int ObHashUtil::get_hash_output_len(const ObHashAlgorithm algo, int64_t &output_len)
{
  int ret = OB_SUCCESS;
  const crypto::OperationResult result = crypto::digest_size(static_cast<uint32_t>(algo));
  if (result.code == crypto::Code::Ok) {
    output_len = static_cast<int64_t>(result.size);
  } else if (result.code == crypto::Code::Allocation) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "cipher type passed");
  }
  return ret;
}

int ObHashUtil::get_sha_hash_algorightm(const int64_t bit_length, ObHashAlgorithm &algo)
{
  int ret = OB_SUCCESS;
  switch(bit_length) {
    case 160:
      algo = OB_HASH_SH1;
      break;
    case 224:
      algo = OB_HASH_SH224;
      break;
    case 256:
      algo = OB_HASH_SH256;
      break;
    case 384:
      algo = OB_HASH_SH384;
      break;
    case 512:
      algo = OB_HASH_SH512;
      break;
    default:
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "sha hash output length");
  }
  return ret;
}

}//end share
} //end oceanbase
