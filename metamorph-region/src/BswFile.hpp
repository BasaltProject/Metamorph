#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace basalt::metamorph::region::bswio {

#if defined(_WIN32)

namespace detail {

inline int openHandle(const std::string& path, unsigned long disposition) {
    const int wide = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wide <= 0) {
        return -1;
    }
    std::wstring name(static_cast<std::size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, name.data(), wide);
    const HANDLE handle =
        CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_BINARY);
    if (fd < 0) {
        CloseHandle(handle);
    }
    return fd;
}

inline HANDLE handleOf(int fd) { return reinterpret_cast<HANDLE>(_get_osfhandle(fd)); }

} // namespace detail

inline int openExisting(const std::string& path) { return detail::openHandle(path, OPEN_EXISTING); }

inline int openNew(const std::string& path) { return detail::openHandle(path, CREATE_ALWAYS); }

inline void closeFile(int fd) { _close(fd); }

inline std::int64_t readAt(int fd, void* into, std::size_t size, std::uint64_t offset) {
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFU);
    at.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD got = 0;
    const DWORD want = static_cast<DWORD>(size < 0x40000000U ? size : 0x40000000U);
    if (ReadFile(detail::handleOf(fd), into, want, &got, &at) == 0) {
        return GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;
    }
    return static_cast<std::int64_t>(got);
}

inline std::int64_t writeAt(int fd, const void* from, std::size_t size, std::uint64_t offset) {
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFU);
    at.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD put = 0;
    const DWORD want = static_cast<DWORD>(size < 0x40000000U ? size : 0x40000000U);
    if (WriteFile(detail::handleOf(fd), from, want, &put, &at) == 0) {
        return -1;
    }
    return static_cast<std::int64_t>(put);
}

inline bool flush(int fd) { return _commit(fd) == 0; }

inline bool truncateTo(int fd, std::uint64_t size) { return _chsize_s(fd, static_cast<long long>(size)) == 0; }

inline std::int64_t sizeOf(int fd) {
    LARGE_INTEGER size{};
    return GetFileSizeEx(detail::handleOf(fd), &size) != 0 ? static_cast<std::int64_t>(size.QuadPart) : -1;
}

inline const std::uint8_t* mapReadOnly(int fd, std::size_t) {
    const HANDLE mapping = CreateFileMappingW(detail::handleOf(fd), nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        return nullptr;
    }
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);
    return static_cast<const std::uint8_t*>(view);
}

inline void unmap(const std::uint8_t* map, std::size_t) { UnmapViewOfFile(map); }

inline void syncDirectory(const std::string&) {}

#else

inline int openExisting(const std::string& path) { return ::open(path.c_str(), O_RDWR); }

inline int openNew(const std::string& path) { return ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644); }

inline void closeFile(int fd) { ::close(fd); }

inline std::int64_t readAt(int fd, void* into, std::size_t size, std::uint64_t offset) {
    return static_cast<std::int64_t>(::pread(fd, into, size, static_cast<off_t>(offset)));
}

inline std::int64_t writeAt(int fd, const void* from, std::size_t size, std::uint64_t offset) {
    return static_cast<std::int64_t>(::pwrite(fd, from, size, static_cast<off_t>(offset)));
}

inline bool flush(int fd) {
#if defined(__APPLE__)
    return ::fsync(fd) == 0;
#else
    return ::fdatasync(fd) == 0;
#endif
}

inline bool truncateTo(int fd, std::uint64_t size) { return ::ftruncate(fd, static_cast<off_t>(size)) == 0; }

inline std::int64_t sizeOf(int fd) {
    struct stat info{};
    return ::fstat(fd, &info) == 0 ? static_cast<std::int64_t>(info.st_size) : -1;
}

inline const std::uint8_t* mapReadOnly(int fd, std::size_t size) {
    void* at = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    return at == MAP_FAILED ? nullptr : static_cast<const std::uint8_t*>(at);
}

inline void unmap(const std::uint8_t* map, std::size_t size) { ::munmap(const_cast<std::uint8_t*>(map), size); }

inline void syncDirectory(const std::string& path) {
    const int fd = ::open(path.empty() ? "." : path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        flush(fd);
        ::close(fd);
    }
}

#endif

} // namespace basalt::metamorph::region::bswio
