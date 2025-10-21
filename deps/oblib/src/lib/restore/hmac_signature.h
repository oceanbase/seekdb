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
#ifndef OB_HMAC_SIGNATURE_H_
#define OB_HMAC_SIGNATURE_H_
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include "lib/container/ob_array.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/string/ob_string_buffer.h"
#include "share/rc/ob_tenant_base.h"
#include "lib/restore/ob_storage_info.h"
namespace oceanbase
{
namespace common
{
int generate_signature_nonce(char *nonce, const int64_t buf_size);

int generate_request_id(char *request_id, const int64_t buf_size);

int base64_encoded(const char *input, const int64_t input_len, char *encoded_result,
    const int64_t encoded_result_buf_len);

int percent_encode(const char *content, char *buf, const int64_t buf_len);

int hmac_sha1(const char *key, const char *data, char *encoded_result, const int64_t encode_buf_len,
    uint32_t &result_len);

int sign_request(ObArray<std::pair<const char *, const char *>> params, const char *method,
    char *signature, const int64_t signature_buf_len);

}  // namespace common
}  // namespace oceanbase
#endif  // OB_HMAC_SIGNATURE_H_
