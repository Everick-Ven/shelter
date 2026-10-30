// SHELTER — безопасная эксклюзивная запись файла мастер-ключа.
#ifndef SHELTER_SECRET_KEY_FILE_H_
#define SHELTER_SECRET_KEY_FILE_H_

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace shelter {
namespace platform {
namespace secret_key_storage {

inline bool WriteAllExclusive(const std::filesystem::path& path,
                              const std::string& data,
                              std::string* error) {
  if (error) error->clear();
  std::string failure;
#if defined(_WIN32)
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    failure = "master-key mirror create failed (os=" +
              std::to_string(static_cast<unsigned long>(code)) + ")";
  } else {
    size_t offset = 0;
    bool ok = true;
    while (offset < data.size()) {
      const size_t remaining = data.size() - offset;
      const DWORD count = static_cast<DWORD>(
          remaining > static_cast<size_t>(MAXDWORD)
              ? MAXDWORD
              : remaining);
      DWORD written = 0;
      if (!WriteFile(file, data.data() + offset, count, &written, nullptr)) {
        const DWORD code = GetLastError();
        failure = "master-key mirror write failed (os=" +
                  std::to_string(static_cast<unsigned long>(code)) + ")";
        ok = false;
        break;
      }
      if (written == 0) {
        failure = "master-key mirror write failed (os=" +
                  std::to_string(static_cast<unsigned long>(ERROR_WRITE_FAULT)) +
                  ")";
        ok = false;
        break;
      }
      offset += written;
    }
    if (ok && !FlushFileBuffers(file)) {
      const DWORD code = GetLastError();
      failure = "master-key mirror flush failed (os=" +
                std::to_string(static_cast<unsigned long>(code)) + ")";
      ok = false;
    }
    if (!CloseHandle(file)) {
      if (ok) {
        const DWORD code = GetLastError();
        failure = "master-key mirror close failed (os=" +
                  std::to_string(static_cast<unsigned long>(code)) + ")";
      }
      ok = false;
    }
    if (!ok) DeleteFileW(path.c_str());
    if (!ok || offset != data.size()) {
      if (error) *error = failure;
      return false;
    }
    return true;
  }
  if (error) *error = failure;
  return false;
#else
  // Эксклюзивное создание запрещает замену ключа; ссылки и наследование
  // дескриптора дочерними процессами блокируются дополнительными флагами.
  int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
  int fd;
  do {
    fd = ::open(path.c_str(), flags, 0600);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) {
    failure = "master-key mirror create failed (errno=" +
              std::to_string(errno) + ")";
    if (error) *error = failure;
    return false;
  }

  size_t offset = 0;
  bool ok = true;
  while (offset < data.size()) {
    const ssize_t written =
        ::write(fd, data.data() + offset, data.size() - offset);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) {
      const int code = written == 0 ? EIO : errno;
      failure = "master-key mirror write failed (errno=" +
                std::to_string(code) + ")";
      ok = false;
      break;
    }
    offset += static_cast<size_t>(written);
  }

  if (ok) {
    int result;
    do {
      result = ::fchmod(fd, 0600);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
      failure = "master-key mirror permissions failed (errno=" +
                std::to_string(errno) + ")";
      ok = false;
    }
  }
  if (ok) {
    int result;
    do {
      result = ::fsync(fd);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
      failure = "master-key mirror flush failed (errno=" +
                std::to_string(errno) + ")";
      ok = false;
    }
  }

  // На EINTR дескриптор мог уже закрыться, поэтому повторять close небезопасно.
  if (::close(fd) != 0 && errno != EINTR) {
    if (ok) {
      failure = "master-key mirror close failed (errno=" +
                std::to_string(errno) + ")";
    }
    ok = false;
  }
  if (!ok) ::unlink(path.c_str());
  if (!ok || offset != data.size()) {
    if (error) *error = failure;
    return false;
  }
  return true;
#endif
}

// Атомарно заменяет уже существующий wrapped-key файл. Временный файл должен
// находиться в том же каталоге и задаваться новым именем; при ошибке записи или
// rename исходный файл остаётся нетронутым.
inline bool ReplaceFileAtomically(const std::filesystem::path& target,
                                  const std::filesystem::path& temporary,
                                  const std::string& data,
                                  std::string* error) {
  if (error) error->clear();
  if (target.empty() || temporary.empty() || target == temporary ||
      target.parent_path().lexically_normal() !=
          temporary.parent_path().lexically_normal()) {
    if (error) *error = "master-key replacement temp must share its directory";
    return false;
  }
  if (!WriteAllExclusive(temporary, data, error)) return false;
#if defined(_WIN32)
  if (!MoveFileExW(temporary.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD code = GetLastError();
    DeleteFileW(temporary.c_str());
    if (error) {
      *error = "master-key replacement failed (os=" +
               std::to_string(static_cast<unsigned long>(code)) + ")";
    }
    return false;
  }
#else
  int result;
  do {
    result = ::rename(temporary.c_str(), target.c_str());
  } while (result != 0 && errno == EINTR);
  if (result != 0) {
    const int code = errno;
    ::unlink(temporary.c_str());
    if (error) {
      *error = "master-key replacement failed (errno=" +
               std::to_string(code) + ")";
    }
    return false;
  }
#endif
  return true;
}

}  // namespace secret_key_storage
}  // namespace platform
}  // namespace shelter

#endif  // SHELTER_SECRET_KEY_FILE_H_
