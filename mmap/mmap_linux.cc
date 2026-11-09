#include "mmap.h"

#include <algorithm>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace my_log {


constexpr size_t kdefaultcapacity = 512 * 1024;

Mmap::Mmap(fpath file_path)
    : file_path_(std::move(file_path)),
      capacity_(0),
      fd_(-1),
      header(nullptr),
      mem_ptr_(nullptr) {
    size_t file_size = 0;
    if (fs::exists(file_path_)) {
        file_size = fs::file_size(file_path_);
    }
    size_t dst_size = (std::max)(file_size, kdefaultcapacity);
    Reserve(dst_size);
    Init_();
}

Mmap::~Mmap() {
    Unmap_();
}

void Mmap::Resize(size_t new_size) {
    if (!IsValid_()) {
        return;
    }
    EnsureCapacity_(new_size);
    header->size = new_size;
}

uint8_t* Mmap::Data() const {
    if (!IsValid_()) {
        return nullptr;
    }
    return static_cast<uint8_t*>(mem_ptr_) + sizeof(Mmapheader);
}

size_t Mmap::Size() const {
    if (!IsValid_()) {
        return 0;
    }
    return header->size;
}

void Mmap::Clear() {
    if (!IsValid_()) {
        return;
    }
    header->size = 0;
}

void Mmap::Push(const void* data, size_t size) {
    if (!IsValid_()) {
        return;
    }
    size_t new_size = Size() + size;
    EnsureCapacity_(new_size);
    memcpy(Data() + Size(), data, size);
    header->size = new_size;
}

double Mmap::GetRatio() const {
    if (!IsValid_()) {
        return 0;
    }
    return static_cast<double>(Size()) / (capacity_ - sizeof(Mmapheader));
}

bool Mmap::Empty() const {
    return Size() == 0;
}

void Mmap::Init_() {
    header = header_();
    if (!header) {
        return;
    }
    if (header->magic != Mmap::kmagic) {
        header->magic = Mmap::kmagic;
        header->size = 0;
    }
}

Mmap::Mmapheader* Mmap::header_() {
    return reinterpret_cast<Mmap::Mmapheader*>(mem_ptr_);
}

void Mmap::Reserve(size_t new_size) {
    if (new_size <= capacity_) {
        return;
    }
    new_size = GetValidCapacity_(new_size);
    if (new_size == capacity_) {
        return;
    }
    Unmap_();
    if (!Trymap_(new_size)) {
        return;
    }
    capacity_ = new_size;
    header = header_();
}

size_t Mmap::GetValidCapacity_(size_t size) {
    size_t page_size = GetPageSize_(size);
    return (size + page_size - 1) / page_size * page_size;
}

size_t Mmap::GetPageSize_(size_t size) {
    return static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

bool Mmap::Trymap_(size_t expect_size) {
    // ① 打开（不存在就创建）文件，拿到文件描述符
    int fd = open(file_path_.string().c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return false;
    }

    // ② 关键：Linux 下 mmap 不会自动撑大文件，必须先 ftruncate
    //    否则访问超过文件末尾的页会直接 SIGBUS 崩溃
    struct stat st {};
    if (fstat(fd, &st) != 0) {
        close(fd);
        return false;
    }
    if (static_cast<size_t>(st.st_size) < expect_size) {
        if (ftruncate(fd, static_cast<off_t>(expect_size)) != 0) {
            close(fd);
            return false;
        }
    }

    // ③ 投影：MAP_SHARED 表示改动会写回文件（MAP_PRIVATE 是写时复制，不落盘）
    void* ptr = mmap(nullptr, expect_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        close(fd);
        return false;
    }

    // ④ 保存资源。注意：Linux 下这里其实就可以 close(fd) 了，映射依然有效；
    //    这里特意保留 fd，是为了后续扩容时还能用 ftruncate 撑大文件
    fd_ = fd;
    mem_ptr_ = ptr;
    capacity_ = expect_size;
    return true;
}

void Mmap::Unmap_() {
    if (mem_ptr_ != nullptr) {
        munmap(mem_ptr_, capacity_);
        mem_ptr_ = nullptr;
        header = nullptr;  // 映射已失效，指针必须一起作废，否则成野指针
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool Mmap::IsValid_() const {
    if (!header) {
        return false;
    }
    return header->magic == Mmap::kmagic;
}

void Mmap::EnsureCapacity_(size_t new_size) {
    size_t real_size = sizeof(Mmapheader) + new_size;
    if (real_size <= capacity_) {
        return;
    }
    Reserve(real_size);
}

}  // namespace my_log
