#if defined(__SWITCH__)
// SQLite on Horizon: pass paths through unchanged.
//
// Both shader caches failed to open with "unable to open database file".
// SQLite's default VFS treats a path as absolute only when it starts with '/',
// and resolves anything else against the working directory - which Horizon does
// not have, so getcwd() fails and the open is reported as SQLITE_CANTOPEN. Our
// paths start with the device prefix "sdmc:/", so every open took that branch.
//
// Everything except xFullPathname delegates to the default VFS; only the
// resolution step is wrong there, not the file I/O.

#include <cstring>
#include <mutex>

#include <sqlite3.h>

namespace {
constexpr const char* kSwitchVfsName = "aurora_switch_passthrough_vfs";

sqlite3_vfs* base_vfs(sqlite3_vfs* vfs) { return static_cast<sqlite3_vfs*>(vfs->pAppData); }

int switch_vfs_open(sqlite3_vfs* vfs, sqlite3_filename name, sqlite3_file* file, int flags,
                    int* outFlags) {
  auto* base = base_vfs(vfs);
  return base->xOpen(base, name, file, flags, outFlags);
}

int switch_vfs_delete(sqlite3_vfs* vfs, const char* name, int syncDir) {
  auto* base = base_vfs(vfs);
  return base->xDelete(base, name, syncDir);
}

int switch_vfs_access(sqlite3_vfs* vfs, const char* name, int flags, int* result) {
  auto* base = base_vfs(vfs);
  return base->xAccess(base, name, flags, result);
}

// The whole point of this VFS: a device-prefixed path is already absolute.
int switch_vfs_full_pathname(sqlite3_vfs*, const char* name, int outSize, char* out) {
  if (name == nullptr || out == nullptr || outSize <= 0) {
    return SQLITE_CANTOPEN;
  }
  sqlite3_snprintf(outSize, out, "%s", name);
  return SQLITE_OK;
}

void* switch_vfs_dl_open(sqlite3_vfs*, const char*) { return nullptr; }

void switch_vfs_dl_error(sqlite3_vfs*, int bytes, char* message) {
  if (message != nullptr && bytes > 0) {
    sqlite3_snprintf(bytes, message, "%s", "Dynamic loading is unsupported");
  }
}

void (*switch_vfs_dl_sym(sqlite3_vfs*, void*, const char*))(void) { return nullptr; }

void switch_vfs_dl_close(sqlite3_vfs*, void*) {}

int switch_vfs_randomness(sqlite3_vfs* vfs, int bytes, char* out) {
  auto* base = base_vfs(vfs);
  return base->xRandomness(base, bytes, out);
}

int switch_vfs_sleep(sqlite3_vfs* vfs, int microseconds) {
  auto* base = base_vfs(vfs);
  return base->xSleep(base, microseconds);
}

int switch_vfs_current_time(sqlite3_vfs* vfs, double* time) {
  auto* base = base_vfs(vfs);
  return base->xCurrentTime(base, time);
}

int switch_vfs_get_last_error(sqlite3_vfs* vfs, int bytes, char* message) {
  auto* base = base_vfs(vfs);
  return base->xGetLastError != nullptr ? base->xGetLastError(base, bytes, message) : SQLITE_OK;
}

int g_registerResult = SQLITE_ERROR;
}  // namespace

// Returns the VFS name to open Switch databases with, or nullptr if it could
// not be registered (callers then fall back to the default VFS).
extern "C" const char* aurora_switch_sqlite_vfs() {
  static std::once_flag registerOnce;
  static sqlite3_vfs switchVfs{};
  std::call_once(registerOnce, [] {
    // Prefer the lock-free unix VFS: Horizon's libc has no working POSIX file
    // locking, and the locking calls the default "unix" VFS makes turned the
    // first write into SQLITE_IOERR ("disk I/O error") even though the open
    // itself succeeded. A shader cache is single-process, so no locking is
    // needed for correctness here.
    auto* base = sqlite3_vfs_find("unix-none");
    if (base == nullptr) {
      base = sqlite3_vfs_find(nullptr);
    }
    if (base == nullptr) {
      return;
    }
    switchVfs.iVersion = 1;
    switchVfs.szOsFile = base->szOsFile;  // The base VFS owns the file handles.
    switchVfs.mxPathname = base->mxPathname;
    switchVfs.pNext = nullptr;
    switchVfs.zName = kSwitchVfsName;
    switchVfs.pAppData = base;
    switchVfs.xOpen = switch_vfs_open;
    switchVfs.xDelete = switch_vfs_delete;
    switchVfs.xAccess = switch_vfs_access;
    switchVfs.xFullPathname = switch_vfs_full_pathname;
    switchVfs.xDlOpen = switch_vfs_dl_open;
    switchVfs.xDlError = switch_vfs_dl_error;
    switchVfs.xDlSym = switch_vfs_dl_sym;
    switchVfs.xDlClose = switch_vfs_dl_close;
    switchVfs.xRandomness = switch_vfs_randomness;
    switchVfs.xSleep = switch_vfs_sleep;
    switchVfs.xCurrentTime = switch_vfs_current_time;
    switchVfs.xGetLastError = switch_vfs_get_last_error;
    g_registerResult = sqlite3_vfs_register(&switchVfs, 0);
  });
  return g_registerResult == SQLITE_OK ? kSwitchVfsName : nullptr;
}
#endif  // __SWITCH__
