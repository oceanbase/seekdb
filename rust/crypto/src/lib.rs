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

mod bridge;

#[cfg(test)]
mod tests;

use bridge::ffi;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    InvalidArgument,
    Allocation,
    Unsupported,
    InvalidCiphertext { backend_code: u64 },
    Backend { backend_code: u64 },
}

impl std::fmt::Display for Error {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InvalidArgument => formatter.write_str("invalid cryptographic argument"),
            Self::Allocation => formatter.write_str("cryptographic allocation failed"),
            Self::Unsupported => formatter.write_str("unsupported cryptographic algorithm"),
            Self::InvalidCiphertext { backend_code } => {
                write!(formatter, "invalid ciphertext ({backend_code:#x})")
            }
            Self::Backend { backend_code } => write!(
                formatter,
                "cryptographic backend failed ({backend_code:#x})"
            ),
        }
    }
}

impl std::error::Error for Error {}
pub type Result<T> = std::result::Result<T, Error>;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum HashAlgorithm {
    Md4 = 1,
    Md5 = 2,
    Sha1 = 3,
    Sha224 = 4,
    Sha256 = 5,
    Sha384 = 6,
    Sha512 = 7,
    Sm3 = 8,
}

impl TryFrom<u32> for HashAlgorithm {
    type Error = Error;
    fn try_from(value: u32) -> Result<Self> {
        match value {
            1 => Ok(Self::Md4),
            2 => Ok(Self::Md5),
            3 => Ok(Self::Sha1),
            4 => Ok(Self::Sha224),
            5 => Ok(Self::Sha256),
            6 => Ok(Self::Sha384),
            7 => Ok(Self::Sha512),
            8 => Ok(Self::Sm3),
            _ => Err(Error::InvalidArgument),
        }
    }
}

impl HashAlgorithm {
    pub const fn digest_size(self) -> usize {
        match self {
            Self::Md4 | Self::Md5 => 16,
            Self::Sha1 => 20,
            Self::Sha224 => 28,
            Self::Sha256 | Self::Sm3 => 32,
            Self::Sha384 => 48,
            Self::Sha512 => 64,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum CipherMode {
    Aes128Ecb = 1,
    Aes192Ecb = 2,
    Aes256Ecb = 3,
    Aes128Cbc = 4,
    Aes192Cbc = 5,
    Aes256Cbc = 6,
    Aes128Cfb1 = 7,
    Aes192Cfb1 = 8,
    Aes256Cfb1 = 9,
    Aes128Cfb8 = 10,
    Aes192Cfb8 = 11,
    Aes256Cfb8 = 12,
    Aes128Cfb128 = 13,
    Aes192Cfb128 = 14,
    Aes256Cfb128 = 15,
    Aes128Ofb = 16,
    Aes192Ofb = 17,
    Aes256Ofb = 18,
    Aes128Gcm = 21,
    Aes192Gcm = 22,
    Aes256Gcm = 23,
    Sm4Cbc = 24,
    Sm4Ecb = 25,
    Sm4Ofb = 26,
    Sm4Cfb128 = 27,
    Sm4Ctr = 28,
    Sm4Gcm = 29,
}

impl TryFrom<u32> for CipherMode {
    type Error = Error;
    fn try_from(value: u32) -> Result<Self> {
        match value {
            1 => Ok(Self::Aes128Ecb),
            2 => Ok(Self::Aes192Ecb),
            3 => Ok(Self::Aes256Ecb),
            4 => Ok(Self::Aes128Cbc),
            5 => Ok(Self::Aes192Cbc),
            6 => Ok(Self::Aes256Cbc),
            7 => Ok(Self::Aes128Cfb1),
            8 => Ok(Self::Aes192Cfb1),
            9 => Ok(Self::Aes256Cfb1),
            10 => Ok(Self::Aes128Cfb8),
            11 => Ok(Self::Aes192Cfb8),
            12 => Ok(Self::Aes256Cfb8),
            13 => Ok(Self::Aes128Cfb128),
            14 => Ok(Self::Aes192Cfb128),
            15 => Ok(Self::Aes256Cfb128),
            16 => Ok(Self::Aes128Ofb),
            17 => Ok(Self::Aes192Ofb),
            18 => Ok(Self::Aes256Ofb),
            20 => Ok(Self::Sm4Cbc),
            21 => Ok(Self::Aes128Gcm),
            22 => Ok(Self::Aes192Gcm),
            23 => Ok(Self::Aes256Gcm),
            24 => Ok(Self::Sm4Cbc),
            25 => Ok(Self::Sm4Ecb),
            26 => Ok(Self::Sm4Ofb),
            27 => Ok(Self::Sm4Cfb128),
            28 => Ok(Self::Sm4Ctr),
            29 => Ok(Self::Sm4Gcm),
            _ => Err(Error::InvalidArgument),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CipherInfo {
    pub key_len: usize,
    pub iv_len: usize,
    pub padding: bool,
    pub aead: bool,
}

fn sdk_result(result: ffi::OperationResult) -> Result<usize> {
    match result.code {
        ffi::Code::Ok => Ok(result.size),
        ffi::Code::InvalidArgument => Err(Error::InvalidArgument),
        ffi::Code::Allocation => Err(Error::Allocation),
        ffi::Code::Unsupported => Err(Error::Unsupported),
        ffi::Code::InvalidCiphertext => Err(Error::InvalidCiphertext {
            backend_code: result.backend_code,
        }),
        _ => Err(Error::Backend {
            backend_code: result.backend_code,
        }),
    }
}

pub fn digest_into(algorithm: HashAlgorithm, input: &[u8], output: &mut [u8]) -> Result<usize> {
    if output.len() < algorithm.digest_size() {
        return Err(Error::InvalidArgument);
    }
    sdk_result(ffi::sdk_digest(algorithm as u32, input, output))
}

pub fn hmac_into(
    algorithm: HashAlgorithm,
    key: &[u8],
    input: &[u8],
    output: &mut [u8],
) -> Result<usize> {
    if output.len() < algorithm.digest_size() || key.len() > i32::MAX as usize {
        return Err(Error::InvalidArgument);
    }
    sdk_result(ffi::sdk_hmac(algorithm as u32, key, input, output))
}

pub fn random_fill(output: &mut [u8]) -> Result<()> {
    if output.len() > i32::MAX as usize {
        return Err(Error::InvalidArgument);
    }
    sdk_result(ffi::sdk_random(output)).map(|_| ())
}

pub fn cipher_info(mode: CipherMode) -> Result<CipherInfo> {
    let info = ffi::sdk_cipher_info(mode as u32);
    sdk_result(info.result)?;
    Ok(CipherInfo {
        key_len: info.key_len,
        iv_len: info.iv_len,
        padding: info.padding,
        aead: info.aead,
    })
}

fn encrypted_size(info: CipherInfo, input_size: usize) -> Result<usize> {
    if info.padding {
        input_size
            .checked_div(16)
            .and_then(|n| n.checked_add(1))
            .and_then(|n| n.checked_mul(16))
            .ok_or(Error::InvalidArgument)
    } else {
        Ok(input_size)
    }
}

pub fn ciphertext_size(mode: CipherMode, input_size: usize) -> Result<usize> {
    encrypted_size(cipher_info(mode)?, input_size)
}

fn validate_cipher(
    info: CipherInfo,
    key: &[u8],
    iv: &[u8],
    aad: &[u8],
    input: &[u8],
    tag_len: usize,
) -> Result<()> {
    if key.len() != info.key_len
        || input.len() > (i32::MAX as usize - 16)
        || (info.iv_len != 0 && !iv.is_empty() && iv.len() != info.iv_len)
        || (info.aead && (tag_len != 16 || aad.len() > i32::MAX as usize))
    {
        Err(Error::InvalidArgument)
    } else {
        Ok(())
    }
}

/// Encrypt with an exact-size key. An empty IV requests the SDK's default IV behavior.
pub fn encrypt_into(
    mode: CipherMode,
    key: &[u8],
    iv: &[u8],
    aad: &[u8],
    input: &[u8],
    output: &mut [u8],
    tag: &mut [u8],
) -> Result<usize> {
    let info = cipher_info(mode)?;
    validate_cipher(info, key, iv, aad, input, tag.len())?;
    if output.len() < encrypted_size(info, input.len())? {
        return Err(Error::InvalidArgument);
    }
    sdk_result(ffi::sdk_encrypt(
        mode as u32,
        key,
        iv,
        aad,
        input,
        output,
        tag,
    ))
}

/// Decrypt with an exact-size key. Failed decryption clears the output buffer.
pub fn decrypt_into(
    mode: CipherMode,
    key: &[u8],
    iv: &[u8],
    aad: &[u8],
    input: &[u8],
    tag: &[u8],
    output: &mut [u8],
) -> Result<usize> {
    let result = (|| {
        let info = cipher_info(mode)?;
        validate_cipher(info, key, iv, aad, input, tag.len())?;
        if output.len() < input.len() {
            return Err(Error::InvalidArgument);
        }
        sdk_result(ffi::sdk_decrypt(
            mode as u32,
            key,
            iv,
            aad,
            input,
            tag,
            output,
        ))
    })();
    if result.is_err() {
        output.fill(0);
    }
    result
}

fn folded_key(mode: CipherMode, key: &[u8], aad: &[u8]) -> Result<([u8; 32], usize)> {
    let info = cipher_info(mode)?;
    if info.key_len == 0 || info.key_len > 32 || (info.aead && !aad.is_empty() && aad.len() != 16) {
        return Err(Error::InvalidArgument);
    }
    let mut folded = [0; 32];
    for (index, byte) in key.iter().enumerate() {
        folded[index % info.key_len] ^= byte;
    }
    Ok((folded, info.key_len))
}

/// Compatibility with ObBlockCipher's cyclic XOR password folding and 16-byte AAD.
pub fn encrypt_folded_into(
    mode: CipherMode,
    key: &[u8],
    iv: &[u8],
    aad: &[u8],
    input: &[u8],
    output: &mut [u8],
    tag: &mut [u8],
) -> Result<usize> {
    let (key, size) = folded_key(mode, key, aad)?;
    encrypt_into(mode, &key[..size], iv, aad, input, output, tag)
}

pub fn decrypt_folded_into(
    mode: CipherMode,
    key: &[u8],
    iv: &[u8],
    aad: &[u8],
    input: &[u8],
    tag: &[u8],
    output: &mut [u8],
) -> Result<usize> {
    let result = (|| {
        let (key, size) = folded_key(mode, key, aad)?;
        decrypt_into(mode, &key[..size], iv, aad, input, tag, output)
    })();
    if result.is_err() {
        output.fill(0);
    }
    result
}

#[derive(Clone, Copy, Debug)]
pub enum DesKey<'a> {
    Default { key_number: u8 },
    Password(&'a [u8]),
}

pub fn is_des_encrypted(input: &[u8]) -> bool {
    input.len() >= 9 && input.len() % 8 == 1 && input[0] & 128 != 0
}

pub fn des_ciphertext_size(input_size: usize) -> Result<usize> {
    if input_size == 0 {
        return Ok(0);
    }
    input_size
        .checked_div(8)
        .and_then(|n| n.checked_add(1))
        .and_then(|n| n.checked_mul(8))
        .and_then(|n| n.checked_add(1))
        .ok_or(Error::InvalidArgument)
}

fn des_key(key: DesKey<'_>) -> Result<(&[u8], bool, u8)> {
    match key {
        DesKey::Default { key_number } if key_number <= 9 => Ok((&[], false, key_number)),
        DesKey::Password(password) if password.len() <= i32::MAX as usize => {
            Ok((password, true, 127))
        }
        _ => Err(Error::InvalidArgument),
    }
}

pub fn des_encrypt_into(input: &[u8], key: DesKey<'_>, output: &mut [u8]) -> Result<usize> {
    if input.is_empty() {
        return Ok(0);
    }
    let (password, has_password, key_number) = des_key(key)?;
    let size = des_ciphertext_size(input.len())?;
    if size - 1 > i32::MAX as usize || output.len() < size {
        return Err(Error::InvalidArgument);
    }
    output[0] = 128 | key_number;
    output[1..1 + input.len()].copy_from_slice(input);
    output[1 + input.len()..size].fill(b'*');
    output[size - 1] = (size - 1 - input.len()) as u8;
    sdk_result(ffi::sdk_des(
        password,
        has_password,
        &mut output[1..size],
        true,
    ))?;
    Ok(size)
}

pub fn des_decrypt_into(input: &[u8], key: DesKey<'_>, output: &mut [u8]) -> Result<usize> {
    let result = (|| {
        if !is_des_encrypted(input) {
            if output.len() < input.len() {
                return Err(Error::InvalidArgument);
            }
            output[..input.len()].copy_from_slice(input);
            return Ok(input.len());
        }
        let (password, has_password, _) = des_key(key)?;
        let size = input.len() - 1;
        if size > i32::MAX as usize || output.len() < size {
            return Err(Error::InvalidArgument);
        }
        output[..size].copy_from_slice(&input[1..]);
        sdk_result(ffi::sdk_des(
            password,
            has_password,
            &mut output[..size],
            false,
        ))?;
        let tail = output[size - 1] as usize;
        if tail > 8 {
            return Err(Error::InvalidCiphertext { backend_code: 0 });
        }
        Ok(size - tail)
    })();
    if result.is_err() {
        output.fill(0);
    }
    result
}

pub fn des_crypt_into(password: &[u8], salt: &[u8], output: &mut [u8]) -> Result<usize> {
    if password.is_empty() {
        return Ok(0);
    }
    if salt.len() < 2
        || output.len() < 13
        || !salt[..2]
            .iter()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'/'))
    {
        return Err(Error::InvalidArgument);
    }
    sdk_result(ffi::sdk_des_crypt(password, &salt[..2], output))
}

/// Expiry of the first PEM certificate, in Unix seconds.
pub fn certificate_expiry(pem: &[u8]) -> Result<i64> {
    if pem.is_empty() || pem.len() > i32::MAX as usize {
        return Err(Error::InvalidArgument);
    }
    let date = ffi::sdk_certificate_expiry(pem);
    sdk_result(date.result)?;
    let year = i64::from(date.year) - i64::from(date.month <= 2);
    let era = year.div_euclid(400);
    let year_of_era = year - era * 400;
    let month = i64::from(date.month) + if date.month > 2 { -3 } else { 9 };
    let day_of_year = (153 * month + 2) / 5 + i64::from(date.day) - 1;
    let days = era * 146097 + year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year
        - 719468;
    Ok(days * 86400
        + i64::from(date.hour) * 3600
        + i64::from(date.minute) * 60
        + i64::from(date.second))
}
