// ============================================================================
// core/update_link.h
// Is a directory entry a LINK the updater must remove rather than enter? A
// junction or directory symlink inside the plugin folder (or the backup folder
// itself being one) would otherwise let a recursive delete reach files outside
// it. Only NAME-SURROGATE reparse points are links: a cloud placeholder
// (OneDrive's IO_REPARSE_TAG_CLOUD_*) is an ordinary folder and is entered.
//
// Plain integers rather than WIN32_FIND_DATAA so the rule compiles and is
// unit-tested without <windows.h> (test_update_link.cpp);
// update_downloader_install.cpp static_asserts the constants against the SDK.
// ============================================================================
#pragma once

#include <cstdint>

namespace UpdateLink {

constexpr uint32_t ATTR_DIRECTORY     = 0x00000010u;  // FILE_ATTRIBUTE_DIRECTORY
constexpr uint32_t ATTR_REPARSE_POINT = 0x00000400u;  // FILE_ATTRIBUTE_REPARSE_POINT
constexpr uint32_t TAG_NAME_SURROGATE = 0x20000000u;  // IsReparseTagNameSurrogate's bit

// `attributes` = dwFileAttributes, `reparseTag` = dwReserved0 of a find result.
constexpr bool isDirectoryLink(uint32_t attributes, uint32_t reparseTag) {
    return (attributes & ATTR_DIRECTORY) != 0
        && (attributes & ATTR_REPARSE_POINT) != 0
        && (reparseTag & TAG_NAME_SURROGATE) != 0;
}

}  // namespace UpdateLink
