#pragma once

#include <stdint.h>

#include <filesystem>

namespace my_log {
namespace fs {

size_t GetFileSize(const std::filesystem::path& file_path);

} // namespace fs
} // namespace my_log
