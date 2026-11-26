// ============================================================================
// tests/unit/test_update_link.cpp
// Guards UpdateLink::isDirectoryLink - which folder entries the updater's
// recursive delete removes as links instead of entering. The integration case
// (updater_test.cpp, "links are removed, never followed") needs a Wine that can
// create directory links, which CI's cannot, so this pins the rule itself.
//
// test_plugin_utils.cpp provides the doctest impl + main
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
// ============================================================================
#include "doctest.h"

#include "core/update_link.h"

#include <cstdint>

namespace {
// Reparse tags as the Windows SDK defines them (winnt.h).
constexpr uint32_t TAG_MOUNT_POINT = 0xA0000003u;  // a junction
constexpr uint32_t TAG_SYMLINK     = 0xA000000Cu;  // a symbolic link
constexpr uint32_t TAG_CLOUD_6     = 0x9000601Au;  // a OneDrive placeholder
constexpr uint32_t TAG_DEDUP       = 0x80000013u;  // a deduplicated file
constexpr uint32_t DIR     = UpdateLink::ATTR_DIRECTORY;
constexpr uint32_t REPARSE = UpdateLink::ATTR_REPARSE_POINT;
}  // namespace

TEST_CASE("update link: junctions and directory symlinks are links") {
    CHECK(UpdateLink::isDirectoryLink(DIR | REPARSE, TAG_MOUNT_POINT));
    CHECK(UpdateLink::isDirectoryLink(DIR | REPARSE, TAG_SYMLINK));
}

TEST_CASE("update link: ordinary folders, cloud placeholders and files are not") {
    CHECK_FALSE(UpdateLink::isDirectoryLink(DIR, 0));
    // dwReserved0 is only a tag for a reparse point: a stray value on a plain
    // folder must not make it a link.
    CHECK_FALSE(UpdateLink::isDirectoryLink(DIR, TAG_MOUNT_POINT));
    // OneDrive's files on demand: a reparse point, but not a name for another
    // folder, so its contents are the plugin's own and are deleted in place.
    CHECK_FALSE(UpdateLink::isDirectoryLink(DIR | REPARSE, TAG_CLOUD_6));
    CHECK_FALSE(UpdateLink::isDirectoryLink(DIR | REPARSE, TAG_DEDUP));
    // A FILE symlink is deleted as a file (DeleteFile removes the link).
    CHECK_FALSE(UpdateLink::isDirectoryLink(REPARSE, TAG_SYMLINK));
}
