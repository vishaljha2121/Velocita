#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" ssize_t compaction_model_write(int, const void*, size_t) noexcept;
extern "C" ssize_t compaction_model_pwrite(int, const void*, size_t, off_t) noexcept;
extern "C" int compaction_model_fsync(int) noexcept;
extern "C" int compaction_model_fdatasync(int) noexcept;
extern "C" int compaction_model_ftruncate(int, off_t) noexcept;
extern "C" int compaction_model_open(const char*, int, ...) noexcept;
extern "C" int compaction_model_openat(int, const char*, int, ...) noexcept;
extern "C" int compaction_model_close(int) noexcept;
extern "C" int compaction_model_rename(const char*, const char*) noexcept;
extern "C" int compaction_model_renameat(int, const char*, int, const char*) noexcept;
extern "C" int compaction_model_mkdirat(int, const char*, mode_t) noexcept;
#if defined(__APPLE__)
extern "C" int compaction_model_renameatx_np(int, const char*, int, const char*, unsigned) noexcept;
#else
extern "C" long compaction_model_syscall(long, ...) noexcept;
#endif

#define write compaction_model_write
#define pwrite compaction_model_pwrite
#define fsync compaction_model_fsync
#define fdatasync compaction_model_fdatasync
#define ftruncate compaction_model_ftruncate
#if !defined(COMPACTION_MODEL_ARCHIVE)
#define open compaction_model_open
#endif
#define openat compaction_model_openat
#define close compaction_model_close
#define rename compaction_model_rename
#define renameat compaction_model_renameat
#define mkdirat compaction_model_mkdirat
#if defined(__APPLE__)
#define renameatx_np compaction_model_renameatx_np
#else
#define syscall compaction_model_syscall
#endif
