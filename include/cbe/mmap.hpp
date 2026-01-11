#pragma once

#include <filesystem>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace catalyst {

/**
 * @brief cross-platform read-only memory mapped file wrapper.
 *
 * Provides access to file contents by mapping them into memory.
 * Handles resource cleanup via RAII.
 * Throws std::runtime_error on failure.
 */
class MappedFile {
public:
    /**
     * @brief Opens and maps the specified file.
     * @param path The path to the file.
     * @throws std::runtime_error If opening, stating, or mapping fails.
     */
    explicit MappedFile(const std::filesystem::path &path) {
#ifdef _WIN32
        file_handle_ = CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file_handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to open file: " + path.string());
        }

        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(file_handle_, &file_size)) {
            CloseHandle(file_handle_);
            throw std::runtime_error("Failed to stat file: " + path.string());
        }
        size_ = static_cast<size_t>(file_size.QuadPart);

        if (size_ == 0) {
            data_ = nullptr;
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return;
        }

        mapping_handle_ = CreateFileMappingW(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_handle_) {
            CloseHandle(file_handle_);
            throw std::runtime_error("Failed to create file mapping: " + path.string());
        }

        void *addr = MapViewOfFile(mapping_handle_, FILE_MAP_READ, 0, 0, 0);
        if (!addr) {
            CloseHandle(mapping_handle_);
            CloseHandle(file_handle_);
            throw std::runtime_error("Failed to map view of file: " + path.string());
        }
        data_ = static_cast<char *>(addr);
#else
        fd_ = open(path.c_str(), O_RDONLY);
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open file: " + path.string());
        }

        struct stat sb;
        if (fstat(fd_, &sb) == -1) {
            close(fd_);
            throw std::runtime_error("Failed to stat file: " + path.string());
        }
        size_ = static_cast<size_t>(sb.st_size);

        if (size_ == 0) {
            data_ = nullptr;
            return;
        }

        posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);

#ifdef __linux__
        void *addr = mmap(nullptr, size_, PROT_READ, MAP_POPULATE | MAP_PRIVATE, fd_, 0);
#else
        void *addr = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
#endif

        if (addr == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to mmap file: " + path.string());
        }
        data_ = static_cast<char *>(addr);
#endif
    }

    ~MappedFile() {
#ifdef _WIN32
        if (data_) {
            UnmapViewOfFile(data_);
        }
        if (mapping_handle_) {
            CloseHandle(mapping_handle_);
        }
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_handle_);
        }
#else
        if (data_) {
            munmap(data_, size_);
        }
        if (fd_ != -1) {
            close(fd_);
        }
#endif
    }

    std::string_view content() const {
        if (!data_)
            return {};
        return {data_, size_};
    }

    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;

private:
#ifdef _WIN32
    HANDLE file_handle_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle_ = nullptr;
#else
    int fd_ = -1;
#endif
    char *data_ = nullptr;
    size_t size_ = 0;
};

} // namespace catalyst
