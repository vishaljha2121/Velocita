#pragma once

#include <cstdio>
#include <unistd.h>

extern "C" ssize_t fault_write(int, const void*, size_t) noexcept;
extern "C" int fault_fsync(int) noexcept;
extern "C" int fault_close(int) noexcept;
extern "C" int fault_rename(const char*, const char*) noexcept;
extern "C" ssize_t fault_pwrite(int, const void*, size_t, off_t) noexcept;
extern "C" int fault_fdatasync(int) noexcept;

#define write fault_write
#define fsync fault_fsync
#define close fault_close
#define rename fault_rename
#define pwrite fault_pwrite
#define fdatasync fault_fdatasync
