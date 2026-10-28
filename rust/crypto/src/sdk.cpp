// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "rust/crypto.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <ctime>
#include <memory>
#include <openssl/asn1.h>
#include <openssl/crypto.h>
#include <openssl/des.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

namespace oceanbase::crypto {
namespace {

OperationResult success(std::size_t size = 0) noexcept
{
  return {Code::Ok, size, 0};
}

OperationResult failure(Code code) noexcept
{
  return {code, 0, static_cast<std::uint64_t>(ERR_get_error())};
}

const EVP_MD *digest(std::uint32_t algorithm) noexcept
{
  switch (algorithm) {
#ifndef OPENSSL_NO_MD4
    case 1: return EVP_md4();
#endif
#ifndef OPENSSL_NO_MD5
    case 2: return EVP_md5();
#endif
    case 3: return EVP_sha1();
    case 4: return EVP_sha224();
    case 5: return EVP_sha256();
    case 6: return EVP_sha384();
    case 7: return EVP_sha512();
#ifndef OPENSSL_NO_SM3
    case 8: return EVP_sm3();
#endif
    default: return nullptr;
  }
}

const EVP_CIPHER *cipher(std::uint32_t mode) noexcept
{
  switch (mode) {
    case 1: return EVP_aes_128_ecb();
    case 2: return EVP_aes_192_ecb();
    case 3: return EVP_aes_256_ecb();
    case 4: return EVP_aes_128_cbc();
    case 5: return EVP_aes_192_cbc();
    case 6: return EVP_aes_256_cbc();
    case 7: return EVP_aes_128_cfb1();
    case 8: return EVP_aes_192_cfb1();
    case 9: return EVP_aes_256_cfb1();
    case 10: return EVP_aes_128_cfb8();
    case 11: return EVP_aes_192_cfb8();
    case 12: return EVP_aes_256_cfb8();
    case 13: return EVP_aes_128_cfb128();
    case 14: return EVP_aes_192_cfb128();
    case 15: return EVP_aes_256_cfb128();
    case 16: return EVP_aes_128_ofb();
    case 17: return EVP_aes_192_ofb();
    case 18: return EVP_aes_256_ofb();
    case 21: return EVP_aes_128_gcm();
    case 22: return EVP_aes_192_gcm();
    case 23: return EVP_aes_256_gcm();
#ifdef OB_USE_BABASSL
    case 24: return EVP_sm4_cbc();
    case 25: return EVP_sm4_ecb();
    case 26: return EVP_sm4_ofb();
    case 27: return EVP_sm4_cfb128();
    case 28: return EVP_sm4_ctr();
    case 29: return EVP_sm4_gcm();
#endif
    default: return nullptr;
  }
}

OperationResult run_cipher(std::uint32_t mode, rust::Slice<const std::uint8_t> key,
                           rust::Slice<const std::uint8_t> iv, rust::Slice<const std::uint8_t> aad,
                           rust::Slice<const std::uint8_t> input, rust::Slice<std::uint8_t> output,
                           std::uint8_t *tag, std::size_t tag_size, bool encrypt) noexcept
{
  ERR_clear_error();
  const EVP_CIPHER *algorithm = cipher(mode);
  if (algorithm == nullptr) { return failure(Code::Unsupported); }
  const int cipher_mode = EVP_CIPHER_mode(algorithm);
  const bool aead = cipher_mode == EVP_CIPH_GCM_MODE;
  const bool padding = cipher_mode == EVP_CIPH_CBC_MODE || cipher_mode == EVP_CIPH_ECB_MODE;
  const auto iv_size = static_cast<std::size_t>(EVP_CIPHER_iv_length(algorithm));
  if (input.size() > INT_MAX - 16 ||
      key.size() != static_cast<std::size_t>(EVP_CIPHER_key_length(algorithm)) ||
      (iv_size != 0 && !iv.empty() && iv.size() != iv_size) ||
      (aead && (tag_size != 16 || aad.size() > INT_MAX))) {
    return failure(Code::InvalidArgument);
  }
  const auto required = encrypt && padding ? (input.size() / 16 + 1) * 16 : input.size();
  if (output.size() < required) { return failure(Code::InvalidArgument); }
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(
      EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!context) { return failure(Code::Allocation); }
  const auto *iv_data = iv_size == 0 || iv.empty() ? nullptr : iv.data();
  if (EVP_CipherInit_ex(context.get(), algorithm, nullptr, key.data(), iv_data, encrypt) != 1 ||
      EVP_CIPHER_CTX_set_padding(context.get(), padding) != 1) {
    return failure(Code::Backend);
  }
  int written = 0;
  if (aead && !aad.empty() && EVP_CipherUpdate(context.get(), nullptr, &written,
                                             aad.data(), static_cast<int>(aad.size())) != 1) {
    return failure(Code::Backend);
  }
  // A non-null scratch pointer also covers empty stream-cipher output slices.
  unsigned char final_block[EVP_MAX_BLOCK_LENGTH] = {};
  auto *destination = output.empty() ? final_block : output.data();
  if (EVP_CipherUpdate(context.get(), destination, &written, input.data(), static_cast<int>(input.size())) != 1) {
    return failure(encrypt ? Code::Backend : Code::InvalidCiphertext);
  }
  if (aead && !encrypt && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, 16, tag) != 1) {
    return failure(Code::Backend);
  }
  int final_size = 0;
  if (EVP_CipherFinal_ex(context.get(), final_block, &final_size) != 1) {
    OPENSSL_cleanse(final_block, sizeof(final_block));
    return failure(encrypt ? Code::Backend : Code::InvalidCiphertext);
  }
  if (written < 0 || final_size < 0 || static_cast<std::size_t>(written) > output.size() ||
      static_cast<std::size_t>(final_size) > output.size() - static_cast<std::size_t>(written)) {
    OPENSSL_cleanse(final_block, sizeof(final_block));
    return failure(Code::Backend);
  }
  if (final_size != 0) {
    std::memcpy(output.data() + written, final_block, static_cast<std::size_t>(final_size));
  }
  OPENSSL_cleanse(final_block, sizeof(final_block));
  if (aead && encrypt && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
    return failure(Code::Backend);
  }
  return success(static_cast<std::size_t>(written) + static_cast<std::size_t>(final_size));
}

}

OperationResult sdk_digest(std::uint32_t algorithm, rust::Slice<const std::uint8_t> input,
                           rust::Slice<std::uint8_t> output) noexcept
{
  ERR_clear_error();
  const EVP_MD *md = digest(algorithm);
  if (md == nullptr) { return failure(Code::Unsupported); }
  if (output.size() < static_cast<std::size_t>(EVP_MD_size(md))) { return failure(Code::InvalidArgument); }
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!context) { return failure(Code::Allocation); }
  unsigned int written = 0;
  if (EVP_DigestInit_ex(context.get(), md, nullptr) != 1 ||
      EVP_DigestUpdate(context.get(), input.data(), input.size()) != 1 ||
      EVP_DigestFinal_ex(context.get(), output.data(), &written) != 1) {
    return failure(Code::Backend);
  }
  return success(written);
}

OperationResult sdk_hmac(std::uint32_t algorithm, rust::Slice<const std::uint8_t> key,
                         rust::Slice<const std::uint8_t> input, rust::Slice<std::uint8_t> output) noexcept
{
  ERR_clear_error();
  const EVP_MD *md = digest(algorithm);
  if (md == nullptr) { return failure(Code::Unsupported); }
  if (key.size() > INT_MAX || output.size() < static_cast<std::size_t>(EVP_MD_size(md))) {
    return failure(Code::InvalidArgument);
  }
  unsigned int written = 0;
  if (HMAC(md, key.data(), static_cast<int>(key.size()), input.data(), input.size(), output.data(), &written) == nullptr) {
    return failure(Code::Backend);
  }
  return success(written);
}

OperationResult sdk_random(rust::Slice<std::uint8_t> output) noexcept
{
  ERR_clear_error();
  if (output.size() > INT_MAX) { return failure(Code::InvalidArgument); }
  if (!output.empty() && RAND_bytes(output.data(), static_cast<int>(output.size())) != 1) {
    OPENSSL_cleanse(output.data(), output.size());
    return failure(Code::Backend);
  }
  return success(output.size());
}

CipherInfo sdk_cipher_info(std::uint32_t mode) noexcept
{
  ERR_clear_error();
  const EVP_CIPHER *algorithm = cipher(mode);
  if (algorithm == nullptr) { return {failure(Code::Unsupported), 0, 0, false, false}; }
  const int cipher_mode = EVP_CIPHER_mode(algorithm);
  return {success(), static_cast<std::size_t>(EVP_CIPHER_key_length(algorithm)),
          static_cast<std::size_t>(EVP_CIPHER_iv_length(algorithm)),
          cipher_mode == EVP_CIPH_ECB_MODE || cipher_mode == EVP_CIPH_CBC_MODE,
          cipher_mode == EVP_CIPH_GCM_MODE};
}

OperationResult sdk_encrypt(std::uint32_t mode, rust::Slice<const std::uint8_t> key,
                            rust::Slice<const std::uint8_t> iv, rust::Slice<const std::uint8_t> aad,
                            rust::Slice<const std::uint8_t> input, rust::Slice<std::uint8_t> output,
                            rust::Slice<std::uint8_t> tag) noexcept
{
  return run_cipher(mode, key, iv, aad, input, output, tag.data(), tag.size(), true);
}

OperationResult sdk_decrypt(std::uint32_t mode, rust::Slice<const std::uint8_t> key,
                            rust::Slice<const std::uint8_t> iv, rust::Slice<const std::uint8_t> aad,
                            rust::Slice<const std::uint8_t> input, rust::Slice<const std::uint8_t> tag,
                            rust::Slice<std::uint8_t> output) noexcept
{
  unsigned char expected_tag[16] = {};
  if (tag.size() == sizeof(expected_tag)) { std::memcpy(expected_tag, tag.data(), sizeof(expected_tag)); }
  auto result = run_cipher(mode, key, iv, aad, input, output, expected_tag, tag.size(), false);
  if (result.code != Code::Ok && !output.empty()) { OPENSSL_cleanse(output.data(), output.size()); }
  return result;
}

OperationResult sdk_des(rust::Slice<const std::uint8_t> password, bool has_password,
                        rust::Slice<std::uint8_t> buffer, bool encrypt) noexcept
{
  ERR_clear_error();
  if (password.size() > INT_MAX || buffer.size() > LONG_MAX || buffer.size() % 8 != 0) {
    return failure(Code::InvalidArgument);
  }
#if defined(OPENSSL_NO_DES) || defined(OPENSSL_NO_MD5)
  return failure(Code::Unsupported);
#else
  DES_key_schedule schedules[3] = {};
  DES_cblock iv = {};
  if (has_password) {
    DES_cblock keys[3] = {};
    const int derived = EVP_BytesToKey(EVP_des_ede3_cbc(), EVP_md5(), nullptr,
                                       password.data(), static_cast<int>(password.size()), 1,
                                       reinterpret_cast<unsigned char *>(keys), iv);
    if (derived != 24) {
      OPENSSL_cleanse(keys, sizeof(keys));
      return failure(Code::Backend);
    }
    for (int i = 0; i < 3; ++i) { DES_set_key_unchecked(&keys[i], &schedules[i]); }
    OPENSSL_cleanse(keys, sizeof(keys));
    std::memset(iv, 0, sizeof(iv));
  }
  DES_ede3_cbc_encrypt(buffer.data(), buffer.data(), static_cast<long>(buffer.size()),
                      &schedules[0], &schedules[1], &schedules[2], &iv, encrypt ? DES_ENCRYPT : DES_DECRYPT);
  OPENSSL_cleanse(schedules, sizeof(schedules));
  return success(buffer.size());
#endif
}

OperationResult sdk_des_crypt(rust::Slice<const std::uint8_t> password,
                              rust::Slice<const std::uint8_t> salt, rust::Slice<std::uint8_t> output) noexcept
{
  ERR_clear_error();
  if (salt.size() != 2 || output.size() < 13) { return failure(Code::InvalidArgument); }
#ifdef OPENSSL_NO_DES
  return failure(Code::Unsupported);
#else
  char text[9] = {};
  char salt_text[3] = {static_cast<char>(salt[0]), static_cast<char>(salt[1]), 0};
  char encrypted[14] = {};
  const auto size = std::min(password.size(), sizeof(text) - 1);
  if (size != 0) { std::memcpy(text, password.data(), size); }
  const bool ok = DES_fcrypt(text, salt_text, encrypted) != nullptr;
  OPENSSL_cleanse(text, sizeof(text));
  if (!ok) { return failure(Code::Backend); }
  const auto written = std::strlen(encrypted);
  std::memcpy(output.data(), encrypted, written);
  return success(written);
#endif
}

CalendarResult sdk_certificate_expiry(rust::Slice<const std::uint8_t> pem) noexcept
{
  ERR_clear_error();
  CalendarResult result = {};
  if (pem.empty() || pem.size() > INT_MAX) {
    result.result = failure(Code::InvalidArgument);
    return result;
  }
  std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
  if (!bio) {
    result.result = failure(Code::Allocation);
    return result;
  }
  std::unique_ptr<X509, decltype(&X509_free)> certificate(
      PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
  if (!certificate) {
    result.result = failure(Code::Backend);
    return result;
  }
  std::tm date = {};
  if (ASN1_TIME_to_tm(X509_get0_notAfter(certificate.get()), &date) != 1) {
    result.result = failure(Code::Backend);
    return result;
  }
  result.result = success();
  result.year = date.tm_year + 1900;
  result.month = date.tm_mon + 1;
  result.day = date.tm_mday;
  result.hour = date.tm_hour;
  result.minute = date.tm_min;
  result.second = date.tm_sec;
  return result;
}

}
