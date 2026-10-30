#ifndef ESTERIA_PLATFORM_PRIVATE_LOCAL_ACCOUNT_FILE_H
#define ESTERIA_PLATFORM_PRIVATE_LOCAL_ACCOUNT_FILE_H

#include <windows.h>
#include "Engine/Source/Platform/LocalAccountHandoff.h"

namespace Esteria::LocalAccount {
// The caller supplies only the fixed executable-sibling path. The private
// launcher directory supplies access control; Windows/Wine metadata checks here
// additionally reject aliases and concurrent readers/writers.
inline bool ReadAndConsume(const wchar_t* path, Credentials& output) {
    output.Clear();
    struct Input {
        unsigned char bytes[InputBytes] = {};
        ~Input() { SecureClear(bytes, sizeof(bytes)); }
    } input;
    struct File {
        HANDLE value = INVALID_HANDLE_VALUE;
        ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    } file;
    file.value = CreateFileW(path, GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) return false;
    const auto shape = [](HANDLE handle, BY_HANDLE_FILE_INFORMATION& info) {
        return GetFileType(handle) == FILE_TYPE_DISK && GetFileInformationByHandle(handle, &info) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            info.nNumberOfLinks == 1 && !info.nFileSizeHigh && info.nFileSizeLow == InputBytes;
    };
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    DWORD read = 0;
    if (!shape(file.value, before) ||
        !ReadFile(file.value, input.bytes, sizeof(input.bytes), &read, nullptr) || read != sizeof(input.bytes) ||
        !shape(file.value, after) || before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
        before.nFileIndexHigh != after.nFileIndexHigh || before.nFileIndexLow != after.nFileIndexLow ||
        before.dwFileAttributes != after.dwFileAttributes) return false;
    Credentials parsed;
    if (!Parse(input.bytes, sizeof(input.bytes), InputKind::Login, parsed)) return false;
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (!SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition))) return false;
    if (!CloseHandle(file.value)) return false;
    file.value = INVALID_HANDLE_VALUE;
    std::memcpy(output.nonce, parsed.nonce, sizeof(output.nonce));
    std::memcpy(output.username, parsed.username, sizeof(output.username));
    std::memcpy(output.password, parsed.password, sizeof(output.password));
    return true;
}
}
#endif
