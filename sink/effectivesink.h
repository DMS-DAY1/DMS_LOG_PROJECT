#pragma once

#include <atomic>

#include <chrono>

#include <filesystem>

#include <mutex>

#include "sink.h"

#include "space.h"

#include "context.h"

#include "crypt.h"

#include "mmap.h"

#include "compress.h"

#include "file_util.h"

namespace my_log {
    namespace detail {
        struct ChunkHeader {
            static constexpr uint64_t kmagic = 0xdeadbeefdada1100;
            uint64_t magic;
            uint64_t size;
            char pub_key[128] = {0};

            ChunkHeader() : magic(kmagic), size(0) {}
        };

        struct ItemHeader {
            static constexpr uint64_t kmagic = 0xbe5fba11;
            uint64_t magic;
            uint64_t size;

            ItemHeader() : magic(kmagic), size(0) {}
        };
    }
        class EffectiveSink final : public LogSink  {
            public:
                struct Conf {
                    std::filesystem::path dif;
                    std::string prefix;
                    std::string pub_key;
                    std::chrono::minutes interval{5};
                    megabytes single_size{4};
                    megabytes total_size{100};
                };

                explicit EffectiveSink(Conf conf);

                ~EffectiveSink() override = default;

                void Log(const LogMsg& msg) override;

                void SetFormatter(std::unique_ptr<Formatter> formatter) override;

                void Flush() override;

                private:

                void SwapCache_();

                bool NeedCacheToFile_();

                void WriteToCache_(const void* data, uint32_t size);

                void PrepareToFile_();

                void CacheToFile_();

                std::filesystem::path GetFilePath_();

                void ElimateFiles_();

                private:
                Conf conf_;
                std::mutex mutex_;
                std::unique_ptr<Formatter> formatter_;
                TaskRunnerTag task_runner_;
                std::unique_ptr<crypt::Crypt> crypt_;
                std::unique_ptr<Compression> compress_;
                std::unique_ptr<Mmap> master_cache_;
                std::unique_ptr<Mmap> slave_cache_;
                std::filesystem::path log_file_path_;
                std::string client_pub_key_;
                std::string compressed_buf_;
                std::string encryped_buf_;
                std::atomic<bool> is_slave_free_{true};
                std::mutex swap_guard_mtx_;
                        
        };
                    
}

