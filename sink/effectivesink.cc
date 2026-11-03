#include "effectivesink.h"

#include "aes_crypt.h"

#include "internal_log.h"

#include "effectiveformatter.h"

#include "zstd_compress.h"

#include "timer_count.h"

#include <fstream>

#include "sys_util.h"

namespace my_log {
    EffectiveSink::EffectiveSink(Conf conf) : conf_(std::move(conf)) {
        LOG_INFO("EffectiveSink: dir={}, prefix={}, pub_key={}, interval={}, single_size()={}, total_size={}",
            conf_.dif, conf_.prefix, conf_.pub_key, conf_.interval, conf_.single_size.count(), conf_.total_size.count());
        if (!std::filesystem::exists(conf_.dif)) {
            std::filesystem::create_directories(conf_.dif);
        }
        task_runner_ = NEW_TASK_RUNNER(10086);
        formatter_ = std::make_unique<EffectiveFormatter>();

        auto ecdh_key = crypt::GenECDHKey();
        auto client_pri = std::get<0>(ecdh_key);
        client_pub_key_ = std::get<1>(ecdh_key);
        LOG_INFO("client pub key size {}", client_pub_key_.size());
        auto ser_pub_key = crypt::HexKeyToBinary(conf_.pub_key);
        auto shared_key = crypt::GenECDHSharedSecret(client_pri, ser_pub_key);
        crypt_ = std::make_unique<crypt::AESCrypt>(shared_key);
        
        compress_ = std::make_unique<compress::ZstdCompress>();
        master_cache_ = std::make_unique<Mmap>(conf_.dif / "master_cache");
        slave_cache_ = std::make_unique<Mmap>(conf_.dif / "slave_cache");
        if (!master_cache_ || !slave_cache_) {
            throw std::runtime_error("EffectiveSink::EffectiveSink  :: create mmap failed");
        }

        if (!slave_cache_->Empty()) {
            is_slave_free_.store(false);
            PrepareToFile_();
            WAIT_TASK_IDLE(task_runner_);
        }
        if (!master_cache_->Empty()) {
            if (is_slave_free_.load()) {
                is_slave_free_.store(false);
                SwapCache_();
            }
            PrepareToFile_();
        }

        POST_REPEATED_TASK(task_runner_, [this]() { ElimateFiles_(); }, -1, conf_.interval);

    }

    void EffectiveSink::Log(const LogMsg& msg) {
        static thread_local MemoryBuf buf;
        formatter_->Format(msg, &buf);

        if (master_cache_->Empty()) {
            compress_->ResetStream();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (master_cache_->Empty()) {
                compress_->ResetStream();
            }
            compressed_buf_.reserve(compress_->CompressedBound(buf.size()));
            size_t compressed_size =
            compress_->Compress(buf.data(), buf.size(), compressed_buf_.data(), compressed_buf_.capacity());
            if (compressed_size == 0) {
                LOG_ERROR("EffectiveSink:Log :: compress failed");
            }
            encryped_buf_.clear();
            crypt_->Encrypt(compressed_buf_.data(), compressed_size, encryped_buf_);
            if (encryped_buf_.empty()) {
                LOG_ERROR("EffectiveSink:Log :: encrypt failed");
            }
            WriteToCache_(encryped_buf_.data(), encryped_buf_.size());
        }
        if (NeedCacheToFile_()) {
            bool do_swap = false;
        {
            std::lock_guard<std::mutex> lk(swap_guard_mtx_);
            if (is_slave_free_.load()) {
                is_slave_free_.store(false);
                SwapCache_();
                do_swap = true;
            }
        }
            if (do_swap) {
                PrepareToFile_();
            }

        }
    }

     bool EffectiveSink::NeedCacheToFile_() {
        return master_cache_->GetRatio() > 0.8;
     }

     void EffectiveSink::WriteToCache_(const void* data, uint32_t size) {
        detail::ItemHeader item;
        item.size = size;
        master_cache_->Push(&item, sizeof(item));
        master_cache_->Push(data, size);
     }

     void EffectiveSink::SwapCache_() {
        std::lock_guard<std::mutex> lock (mutex_);
        std::swap(master_cache_, slave_cache_);
     }

     void EffectiveSink::PrepareToFile_() {
        POST_TASK(task_runner_, [this]() {CacheToFile_();});
     }

     void EffectiveSink::CacheToFile_() {
        TIMER_COUNT("CacheToFile");
        if (is_slave_free_.load()) {
            return;
        }
        if (slave_cache_->Empty()) {
            is_slave_free_.store(true);
            return;
        }

        {
            auto file_path = GetFilePath_();
            detail::ChunkHeader chunk;
            chunk.size = slave_cache_->Size();
            memcpy(chunk.pub_key, client_pub_key_.data(), client_pub_key_.size());
            std::ofstream ofs(file_path, std::ios::binary | std::ios::app);
            ofs.write(reinterpret_cast<char*>(&chunk), sizeof(chunk));
            ofs.write(reinterpret_cast<char*>(slave_cache_->Data()), slave_cache_->Size());
            ofs.close();
        }
        slave_cache_->Clear();
        is_slave_free_.store(true);
     }

     std::filesystem::path EffectiveSink::GetFilePath_() {
        auto GetDateTimePath = [this] () {
            std::time_t now = std::time(nullptr);
            std::tm tm;
            LocalTime(&tm, &now);
            char time_buf[32] = {};
            std::strftime(time_buf, sizeof(time_buf), "%Y%m%d%H%M%S", &tm);
            return (conf_.dif / (conf_.prefix + "_" + time_buf) );
        };
        if (log_file_path_.empty()) {
            log_file_path_ = GetDateTimePath().string() + ".log";
        } else {
            auto file_size = fs::GetFileSize(log_file_path_);
            bytes single_bytes = space_cast<bytes>(conf_.single_size);
            if (file_size > single_bytes.count()) {
                auto date_time_path = GetDateTimePath();
                auto file_path = date_time_path.string() + ".log";
                if (std::filesystem::exists(file_path)) {
                    int index = 0;
                    for (auto& p : std::filesystem::directory_iterator(file_path)) {
                        if (p.path().filename().string().find(date_time_path.filename().string()) != std::string::npos) {
                            index++;
                        }
                    }
                    log_file_path_ = date_time_path.string() + "_" + std::to_string(index) + ".log";
                } else {
                    log_file_path_ = file_path;
                }
                
            }
        }

        LOG_INFO("EffectiveSink::GetFilePath_ : log_file_path_={}", log_file_path_.string());
        return log_file_path_;
     }

      void EffectiveSink::ElimateFiles_() {
          LOG_INFO("EffectiveSink::ElimateFiles : start");
          std::vector<std::filesystem::path> files;
          for (auto& p : std::filesystem::directory_iterator(conf_.dif)) {
            if (p.path().extension() == ".log") {
                files.push_back(p.path());
            }
          }
          std::sort(files.begin(), files.end(), [] (const std::filesystem::path& e, const std::filesystem::path& i) {
              return std::filesystem::last_write_time(e) > std::filesystem::last_write_time(i);
          });
          size_t total_size = space_cast<bytes>(conf_.total_size).count();
          size_t used_size = 0;
          for (auto& file : files) {
            used_size += fs::GetFileSize(file);
            if (used_size > total_size) {
                std::filesystem::remove(file);
            }
          }
      }

      void EffectiveSink::SetFormatter(std::unique_ptr<Formatter> formatter) {
      }

      void EffectiveSink::Flush() {
          TIMER_COUNT("Flush");
          PrepareToFile_();
          WAIT_TASK_IDLE(task_runner_);

          bool do_swap = false;
        {
            std::lock_guard<std::mutex> lk(swap_guard_mtx_);
            if (is_slave_free_.load()) {
                is_slave_free_.store(false);
                SwapCache_();
                do_swap = true;
            }
        }
            if (do_swap) {
                PrepareToFile_();
            }

          WAIT_TASK_IDLE(task_runner_);
      }
}