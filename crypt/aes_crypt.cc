#include "crypt/aes_crypt.h"

#include <cstdint>
#include <stdexcept>

#include "cryptopp/aes.h"
#include "cryptopp/cryptlib.h"
#include "cryptopp/filters.h"
#include "cryptopp/hex.h"
#include "cryptopp/modes.h"
#include "cryptopp/osrng.h"

namespace my_log {
    namespace crypt {
        namespace {
            using CryptoPP::byte;

            constexpr size_t kIVSize = CryptoPP::AES::BLOCKSIZE;
            constexpr size_t kLengthFieldSize = 4;
            constexpr size_t kFrameHeaderSize = kIVSize + kLengthFieldSize;

            void WriteBigEndian32At(std::string& buf, size_t pos, uint32_t value) {
                for (size_t i = 0; i < kLengthFieldSize; ++i) {
                    buf[pos + i] = static_cast<char>((value >> (24 - 8 * i)) & 0xFF);
                }
            }

            uint32_t ReadBigEndian32(const byte* p) {
                return (static_cast<uint32_t>(p[0]) << 24) |
                       (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8) |
                       (static_cast<uint32_t>(p[3]));
            }
        }

        namespace detail {
            using CryptoPP::byte;

            std::string GenerateKey() {
                CryptoPP::AutoSeededRandomPool rnd;
                byte key[CryptoPP::AES::DEFAULT_KEYLENGTH];
                rnd.GenerateBlock(key, sizeof(key));
                return BinaryKeyToHex(std::string(reinterpret_cast<const char*>(key), sizeof(key)));
            }

            std::string GenerateIV() {
                CryptoPP::AutoSeededRandomPool rnd;
                byte iv[CryptoPP::AES::BLOCKSIZE];
                rnd.GenerateBlock(iv, sizeof(iv));
                return std::string(reinterpret_cast<const char*>(iv), sizeof(iv));
            }

            std::string DecryptFrame(const byte* cipher, size_t cipher_size,
                                     const std::string& key, const byte* iv) {
                std::string plaintext;
                CryptoPP::AES::Decryption aes_decryption(reinterpret_cast<const byte*>(key.data()), key.size());
                CryptoPP::CBC_Mode_ExternalCipher::Decryption cbc_decryption(aes_decryption, iv);

                CryptoPP::StreamTransformationFilter stf_decryptor(cbc_decryption,
                    new CryptoPP::StringSink(plaintext));
                stf_decryptor.Put(cipher, cipher_size);
                stf_decryptor.MessageEnd();
                return plaintext;
            }
        }

        AESCrypt::AESCrypt(std::string key) : key_(std::move(key)) {}

        void AESCrypt::Encrypt(const void* input, size_t input_size, std::string& output) {
            if (input == nullptr) {
                throw std::invalid_argument("AESCrypt::Encrypt: input is null");
            }

            const std::string iv = detail::GenerateIV();

            output.append(iv.data(), iv.size());
            const size_t length_field_pos = output.size();
            output.append(kLengthFieldSize, '\0');
            const size_t cipher_begin = output.size();

            {
                CryptoPP::AES::Encryption aes_encryption(reinterpret_cast<const byte*>(key_.data()), key_.size());
                CryptoPP::CBC_Mode_ExternalCipher::Encryption cbc_encryption(aes_encryption,
                    reinterpret_cast<const byte*>(iv.data()));

                CryptoPP::StreamTransformationFilter stf_encryptor(cbc_encryption,
                    new CryptoPP::StringSink(output));
                stf_encryptor.Put(reinterpret_cast<const byte*>(input), input_size);
                stf_encryptor.MessageEnd();
            }

            const size_t cipher_size = output.size() - cipher_begin;
            if (cipher_size > 0xFFFFFFFFu) {
                throw std::length_error("AESCrypt::Encrypt: 单帧密文超过 4GB,需分多次加密");
            }
            WriteBigEndian32At(output, length_field_pos, static_cast<uint32_t>(cipher_size));
        }

        std::string AESCrypt::Decrypt(const void* data, size_t size) {
            if (data == nullptr) {
                return "";
            }

            const byte* p = static_cast<const byte*>(data);
            size_t pos = 0;
            std::string plaintext;

            while (size - pos >= kFrameHeaderSize) {
                const byte* frame_iv = p + pos;
                const uint32_t cipher_size = ReadBigEndian32(p + pos + kIVSize);
                pos += kFrameHeaderSize;

                if (cipher_size > size - pos) {
                    throw std::runtime_error("AESCrypt::Decrypt: 帧不完整（数据被截断）");
                }

                plaintext.append(detail::DecryptFrame(p + pos, cipher_size, key_, frame_iv));
                pos += static_cast<size_t>(cipher_size);
            }

            if (pos != size) {
                throw std::runtime_error("AESCrypt::Decrypt: 尾部残留不足一帧头的数据");
            }

            return plaintext;
        }

        std::string AESCrypt::GenerateKey() {
            return detail::GenerateKey();
        }

        std::string AESCrypt::GenerateIV() {
            return detail::GenerateIV();
        }
    }
}
