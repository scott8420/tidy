#include "core/Facts.hpp"

#include <latch/Door.hpp>   // tag_of: reads a user.tidy.* xattr, never following a symlink

#include <cstdio>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>   // makedev

namespace tidy::core::facts {

Tags tags_of(const std::string& path) {
    Tags t;
    latch::door::tag_of(path, kId, t.id);
    latch::door::tag_of(path, kRetention, t.retention);
    latch::door::tag_of(path, kPurpose, t.purpose);
    latch::door::tag_of(path, kRole, t.role);
    return t;
}

bool of(const std::string& path, Facts& out) {
    struct statx sx{};
    if (::statx(AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW,
                STATX_BASIC_STATS | STATX_BTIME, &sx) != 0)
        return false;
    out = Facts{};
    out.path = path;
    // The same number lstat() gives (latch's identity uses st_dev): makedev.
    out.dev = static_cast<std::uint64_t>(makedev(sx.stx_dev_major, sx.stx_dev_minor));
    out.ino = sx.stx_ino;
    out.size = sx.stx_size;
    out.mtime = sx.stx_mtime.tv_sec;
    out.ctime = sx.stx_ctime.tv_sec;
    out.atime = sx.stx_atime.tv_sec;
    out.has_btime = (sx.stx_mask & STATX_BTIME) != 0;
    out.btime = out.has_btime ? sx.stx_btime.tv_sec : 0;
    out.dir = S_ISDIR(sx.stx_mode);
    out.tags = tags_of(path);
    return true;
}

std::string new_id() {
    unsigned char b[16];
    std::size_t got = 0;
    while (got < sizeof b) {
        const ssize_t n = ::getrandom(b + got, sizeof b - got, 0);
        if (n > 0) got += static_cast<std::size_t>(n);
    }
    b[6] = static_cast<unsigned char>((b[6] & 0x0f) | 0x40);   // version 4
    b[8] = static_cast<unsigned char>((b[8] & 0x3f) | 0x80);   // RFC 4122 variant
    char s[37];
    std::snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return s;
}

bool is_id(const std::string& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

}  // namespace tidy::core::facts
