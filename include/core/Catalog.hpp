#pragma once
// core::catalog -- Tidy's OWN record of the files it knows (s004, REQUIREMENTS
// §7): SQLite at <data>/catalog.db, keyed by the stable id (user.tidy.id), the
// inode (dev, ino) as a fast lookup. Like Rouster's manifest it is Tidy's own
// file, written by Tidy -- not a user file, so not through the door (latch
// refuses it as a target: "Tidy's own records").
//
// Catalog.cpp is the ONLY Tidy source the gate lets touch SQLite, and the only
// one allowed one filesystem write: creating the data folder.
//
// The catalog is a record of what was SEEN (facts as of `seen`), not the truth
// on disk; the tags on the file are the truth, and record() reads them back.
#include "core/Facts.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace tidy::core::catalog {

struct Entry {
    std::string   id;              // the stable id (UUID)
    std::string   path;
    std::uint64_t dev = 0, ino = 0, size = 0;
    std::int64_t  btime = 0, mtime = 0, ctime = 0, atime = 0;
    std::string   retention, purpose, role;
    std::int64_t  seen = 0;        // unix seconds Tidy last read it
    bool          gone = false;    // not at its path, nor anywhere Tidy looked, when last seen
};

Entry from_facts(const facts::Facts& f, std::int64_t seen);

// s010e: a project's row -- keyed by its .tidy-project id. Where it was seen
// (path, name, template) is refreshed at each Look; status and outcome are
// the user's and only set_status() changes them.
struct ProjectRow {
    std::string  id, path, name, tmpl;
    std::string  status = "active";
    std::string  outcome;
    std::int64_t seen = 0;
    bool         gone = false;
};

class Catalog {
public:
    Catalog() = default;
    ~Catalog();
    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;

    static std::string path_in(const std::string& data_dir);   // <data>/catalog.db

    // create=false: read-only, and only if it exists (the app at launch).
    bool open(const std::string& data_dir, bool create, std::string* err = nullptr);
    bool is_open() const { return m_db != nullptr; }
    bool writable() const { return m_rw; }

    bool put(const Entry& e);                 // insert or replace, by id
    bool mark_gone(const std::string& id, std::int64_t when);
    bool drop(const std::string& id);         // the file no longer carries the id (a stamp undone)
    std::optional<Entry> by_id(const std::string& id) const;
    std::optional<Entry> by_inode(std::uint64_t dev, std::uint64_t ino) const;
    std::optional<Entry> by_path(const std::string& path) const;   // the newest seen there
    std::vector<Entry> all() const;
    std::size_t count(bool include_gone = false) const;

    // Projects (s010e). seen_project keeps status / outcome; a new id starts active.
    bool seen_project(const ProjectRow& r);
    bool set_status(const std::string& id, const std::string& status, const std::string& outcome);
    std::optional<ProjectRow> project(const std::string& id) const;
    std::vector<ProjectRow> projects() const;

private:
    std::vector<Entry> query(const char* sql, const std::vector<std::string>& args) const;
    sqlite3* m_db = nullptr;
    bool m_rw = false;
};

}  // namespace tidy::core::catalog
