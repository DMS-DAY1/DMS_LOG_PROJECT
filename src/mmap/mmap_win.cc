#include "mmap.h"

#include <algorithm>

namespace fs = std::filesystem;

constexpr size_t kdefaultcapacity = 512 * 1024;

Mmap::Mmap(fpath file_path ) : file_path_(std::move(file_path)), handle_(nullptr), capacity_(0), mem_ptr_(nullptr), file_handle_(nullptr), header(nullptr) {
    
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

void Mmap:: Resize(size_t new_size) {
    if (!IsValid_()) {
        return;
    }
    EnsureCapacity_(new_size);
    header->size = new_size;
}

void Mmap::Clear() {
    if (!IsValid_()) {
        return;
    }
    header->size = 0;
}

uint8_t* Mmap::Data() const {
    if (!IsValid_()) {
        return nullptr;
    }
    return static_cast<uint8_t*> (mem_ptr_) + sizeof(Mmapheader);

}

size_t Mmap::Size() const {
    if (!IsValid_()) {
        return 0;
    }
    return header->size;
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
    return reinterpret_cast<Mmap::Mmapheader*> (mem_ptr_);
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
    size_t page_size =  GetPageSize_(size);
    return (size + page_size - 1) / page_size * page_size; 
}

size_t Mmap::GetPageSize_(size_t size) {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        return info.dwPageSize;
    }

bool Mmap::Trymap_(size_t expect_size) {
    HANDLE hfile = CreateFileA(
        file_path_.string().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hfile == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD high = static_cast<DWORD> (expect_size >> 32);
    DWORD low = static_cast<DWORD> (expect_size & 0xFFFFFFFFULL);
    HANDLE hmap = CreateFileMappingA(
        hfile,
        nullptr,
        PAGE_READWRITE,
        high, low,
        nullptr
    );
    if (hmap == nullptr) {
        CloseHandle(hfile);
        return false;
    }
    void* ptr = MapViewOfFile(
        hmap,
        FILE_MAP_ALL_ACCESS,
        0,0,
        expect_size
    );
    if (ptr == nullptr) {
        CloseHandle(hfile);
        CloseHandle(hmap);
        return false;

    }
    file_handle_ = hfile;
    handle_ = hmap;
    mem_ptr_ = ptr;
    capacity_ = expect_size;
    return true;
}

void Mmap::Unmap_() {
    if (mem_ptr_ != nullptr) {
        UnmapViewOfFile(mem_ptr_);
        mem_ptr_ = nullptr;
    }
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    if (file_handle_ != nullptr) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
    }
    header = nullptr;
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
};
