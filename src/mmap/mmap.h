#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#include <sysinfoapi.h>
#endif

namespace my_log {


class Mmap {
public:
    using fpath = std::filesystem::path;

    explicit Mmap(fpath file_path);

    ~Mmap();

    Mmap(const Mmap&) = delete;

    Mmap& operator=(const Mmap&) = delete;

    void Resize(size_t new_size);

    uint8_t* Data() const;

    size_t Size() const;

    void Clear();

    void Push(const void* data, size_t size);

    double GetRatio() const;

    bool Empty() const;

    void Reserve(size_t new_size);

    struct Mmapheader {
        uint32_t magic;
        uint64_t size;
    };

private:
    static constexpr uint32_t kmagic = 0x4C4F474D;

    fpath file_path_;

    size_t capacity_;

    
#ifdef _WIN32
    HANDLE handle_;       
    HANDLE file_handle_;  
#else
    int fd_ = -1;
#endif

    Mmapheader* header;

    void* mem_ptr_;

    bool Trymap_(size_t expect_size);

    void Unmap_();

    void Init_();

    static size_t GetValidCapacity_(size_t size);

    static size_t GetPageSize_(size_t size);

    Mmapheader* header_();

    bool IsValid_() const;

    void EnsureCapacity_(size_t new_size);
};

}  // namespace my_log
