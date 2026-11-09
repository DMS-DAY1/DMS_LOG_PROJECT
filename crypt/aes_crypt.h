#pragma once

#include <string>

#include "crypt/crypt.h"

namespace my_log {
    namespace crypt {

        class AESCrypt final : public Crypt {
        public:
            explicit AESCrypt(std::string key);
            ~AESCrypt() override = default;

            AESCrypt(const AESCrypt&) = delete;
            AESCrypt& operator=(const AESCrypt&) = delete;

            static std::string GenerateKey();

            static std::string GenerateIV();

            void Encrypt(const void* input, size_t input_size, std::string& output) override;

            std::string Decrypt(const void* data, size_t size) override;

        private:
            std::string key_;
        };
    }
}
