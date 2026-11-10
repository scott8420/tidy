#pragma once
// core::facts -- what Tidy knows about one file, read, never written (s004,
// REQUIREMENTS §7). GTK-free.
//
//   the inode   statx: dev + ino (the fast lookup), size, and four times --
//               btime (birth; resets on every atomic save, cannot be copied),
//               mtime (content), ctime (your touch: rename, tag, chmod),
//               atime (reads; relatime, about daily -- "dormant 6 months",
//               not "read this morning").
//   the tags    user.tidy.{id, retention, purpose, role}, read with latch's
//               tag_of. They are SET only through latch's door (the Tag verb).
#include <cstdint>
#include <string>

namespace tidy::core::facts {

inline constexpr const char* kId        = "user.tidy.id";
inline constexpr const char* kRetention = "user.tidy.retention";
inline constexpr const char* kPurpose   = "user.tidy.purpose";
inline constexpr const char* kRole      = "user.tidy.role";

struct Tags {
    std::string id, retention, purpose, role;
    bool any() const { return !id.empty() || !retention.empty() || !purpose.empty() || !role.empty(); }
    bool operator==(const Tags&) const = default;
};

struct Facts {
    std::string   path;
    std::uint64_t dev = 0, ino = 0, size = 0;
    std::int64_t  btime = 0, mtime = 0, ctime = 0, atime = 0;   // unix seconds; btime 0 = unknown
    bool          has_btime = false;
    bool          dir = false;
    Tags          tags;
};

// statx (never following a symlink) + the tags. false = gone or unreadable.
bool of(const std::string& path, Facts& out);
Tags tags_of(const std::string& path);

// A new stable id: a random (v4) UUID, lowercase, 36 chars.
std::string new_id();
bool is_id(const std::string& s);

}  // namespace tidy::core::facts
