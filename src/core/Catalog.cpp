#include "core/Catalog.hpp"

#include <sqlite3.h>

#include <filesystem>

// Catalog.cpp -- the one Tidy source that writes SQLite (Tidy's own record),
// and the one allowed filesystem write: the data folder, created on first open
// for writing. The selftest's source gate allows exactly these, here only.

namespace tidy::core::catalog {

namespace fs = std::filesystem;

namespace {
constexpr const char* kSchema =
    "CREATE TABLE IF NOT EXISTS files ("
    "  id TEXT PRIMARY KEY,"
    "  path TEXT NOT NULL,"
    "  dev INTEGER NOT NULL, ino INTEGER NOT NULL, size INTEGER NOT NULL,"
    "  btime INTEGER NOT NULL, mtime INTEGER NOT NULL, ctime INTEGER NOT NULL, atime INTEGER NOT NULL,"
    "  retention TEXT NOT NULL DEFAULT '', purpose TEXT NOT NULL DEFAULT '', role TEXT NOT NULL DEFAULT '',"
    "  seen INTEGER NOT NULL, gone INTEGER NOT NULL DEFAULT 0);"
    "CREATE INDEX IF NOT EXISTS files_inode ON files(dev, ino);"
    "CREATE INDEX IF NOT EXISTS files_path ON files(path);"
    "CREATE TABLE IF NOT EXISTS projects ("        /* s010e */
    "  id TEXT PRIMARY KEY, path TEXT NOT NULL, name TEXT NOT NULL DEFAULT '', tmpl TEXT NOT NULL DEFAULT '',"
    "  status TEXT NOT NULL DEFAULT 'active', outcome TEXT NOT NULL DEFAULT '',"
    "  seen INTEGER NOT NULL DEFAULT 0, gone INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS uses ("            /* s010g */
    "  project TEXT NOT NULL, used TEXT NOT NULL, PRIMARY KEY (project, used));"
    "CREATE TABLE IF NOT EXISTS archived ("        /* s013a */
    "  id TEXT PRIMARY KEY, path TEXT NOT NULL, origin TEXT NOT NULL DEFAULT '',"
    "  sha256 TEXT NOT NULL, size INTEGER NOT NULL DEFAULT 0, archived INTEGER NOT NULL DEFAULT 0,"
    "  batch INTEGER NOT NULL DEFAULT 0, media TEXT NOT NULL DEFAULT '', proved INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS copies ("          /* s013b */
    "  id TEXT NOT NULL, media TEXT NOT NULL, path TEXT NOT NULL, sha256 TEXT NOT NULL,"
    "  size INTEGER NOT NULL DEFAULT 0, proved INTEGER NOT NULL DEFAULT 0, batch INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (id, media));"
    "PRAGMA user_version = 5;";

constexpr const char* kCols = "id, path, dev, ino, size, btime, mtime, ctime, atime, retention, purpose, role, seen, gone";

std::int64_t s64(std::uint64_t v) { return static_cast<std::int64_t>(v); }
std::uint64_t u64(std::int64_t v) { return static_cast<std::uint64_t>(v); }
std::string text(sqlite3_stmt* st, int i) {
    const auto* p = sqlite3_column_text(st, i);
    return p ? reinterpret_cast<const char*>(p) : "";
}
Entry row(sqlite3_stmt* st) {
    Entry e;
    e.id = text(st, 0);
    e.path = text(st, 1);
    e.dev = u64(sqlite3_column_int64(st, 2));
    e.ino = u64(sqlite3_column_int64(st, 3));
    e.size = u64(sqlite3_column_int64(st, 4));
    e.btime = sqlite3_column_int64(st, 5);
    e.mtime = sqlite3_column_int64(st, 6);
    e.ctime = sqlite3_column_int64(st, 7);
    e.atime = sqlite3_column_int64(st, 8);
    e.retention = text(st, 9);
    e.purpose = text(st, 10);
    e.role = text(st, 11);
    e.seen = sqlite3_column_int64(st, 12);
    e.gone = sqlite3_column_int(st, 13) != 0;
    return e;
}
}  // namespace

Entry from_facts(const facts::Facts& f, std::int64_t seen) {
    Entry e;
    e.id = f.tags.id;
    e.path = f.path;
    e.dev = f.dev;
    e.ino = f.ino;
    e.size = f.size;
    e.btime = f.btime;
    e.mtime = f.mtime;
    e.ctime = f.ctime;
    e.atime = f.atime;
    e.retention = f.tags.retention;
    e.purpose = f.tags.purpose;
    e.role = f.tags.role;
    e.seen = seen;
    return e;
}

Catalog::~Catalog() {
    if (m_db) sqlite3_close(m_db);
}

std::string Catalog::path_in(const std::string& data_dir) { return data_dir + "/catalog.db"; }

bool Catalog::open(const std::string& data_dir, bool create, std::string* err) {
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
    const std::string p = path_in(data_dir);
    std::error_code ec;
    if (create) {
        fs::create_directories(data_dir, ec);
        if (ec) { if (err) *err = "could not make " + data_dir + ": " + ec.message(); return false; }
    } else if (!fs::exists(p, ec)) {
        if (err) *err = "no catalog yet";
        return false;
    }
    const int flags = create ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) : SQLITE_OPEN_READONLY;
    if (sqlite3_open_v2(p.c_str(), &m_db, flags, nullptr) != SQLITE_OK) {
        if (err) *err = m_db ? sqlite3_errmsg(m_db) : "sqlite open failed";
        if (m_db) sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }
    m_rw = create;
    sqlite3_busy_timeout(m_db, 2000);
    if (create) {
        char* msg = nullptr;
        if (sqlite3_exec(m_db, kSchema, nullptr, nullptr, &msg) != SQLITE_OK) {
            if (err) *err = msg ? msg : "schema failed";
            sqlite3_free(msg);
            sqlite3_close(m_db);
            m_db = nullptr;
            return false;
        }
    }
    return true;
}

bool Catalog::put(const Entry& e) {
    if (!m_db || !m_rw || e.id.empty()) return false;
    const std::string sql = std::string("INSERT OR REPLACE INTO files (") + kCols +
                            ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, e.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, e.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, s64(e.dev));
    sqlite3_bind_int64(st, 4, s64(e.ino));
    sqlite3_bind_int64(st, 5, s64(e.size));
    sqlite3_bind_int64(st, 6, e.btime);
    sqlite3_bind_int64(st, 7, e.mtime);
    sqlite3_bind_int64(st, 8, e.ctime);
    sqlite3_bind_int64(st, 9, e.atime);
    sqlite3_bind_text(st, 10, e.retention.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 11, e.purpose.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 12, e.role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 13, e.seen);
    sqlite3_bind_int(st, 14, e.gone ? 1 : 0);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool Catalog::mark_gone(const std::string& id, std::int64_t when) {
    if (!m_db || !m_rw) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE files SET gone = 1, seen = ? WHERE id = ?", -1, &st, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, when);
    sqlite3_bind_text(st, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(m_db) == 1;
    sqlite3_finalize(st);
    return ok;
}

bool Catalog::drop(const std::string& id) {
    if (!m_db || !m_rw) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM files WHERE id = ?", -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(m_db) == 1;
    sqlite3_finalize(st);
    return ok;
}

std::vector<Entry> Catalog::query(const char* where, const std::vector<std::string>& args) const {
    std::vector<Entry> out;
    if (!m_db) return out;
    const std::string sql = std::string("SELECT ") + kCols + " FROM files " + where;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;
    for (std::size_t i = 0; i < args.size(); ++i)
        sqlite3_bind_text(st, static_cast<int>(i + 1), args[i].c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(row(st));
    sqlite3_finalize(st);
    return out;
}

std::optional<Entry> Catalog::by_id(const std::string& id) const {
    auto v = query("WHERE id = ?", {id});
    if (v.empty()) return std::nullopt;
    return v.front();
}

std::optional<Entry> Catalog::by_inode(std::uint64_t dev, std::uint64_t ino) const {
    // Bound as text: SQLite compares an INTEGER column with a numeric text value by number.
    auto v = query("WHERE dev = ? AND ino = ? AND gone = 0 ORDER BY seen DESC",
                   {std::to_string(s64(dev)), std::to_string(s64(ino))});
    if (v.empty()) return std::nullopt;
    return v.front();
}

std::optional<Entry> Catalog::by_path(const std::string& path) const {
    auto v = query("WHERE path = ? ORDER BY gone ASC, seen DESC", {path});
    if (v.empty()) return std::nullopt;
    return v.front();
}

std::vector<Entry> Catalog::all() const { return query("ORDER BY path", {}); }

std::size_t Catalog::count(bool include_gone) const {
    return query(include_gone ? "" : "WHERE gone = 0", {}).size();
}

// ── Projects (s010e) ────────────────────────────────────────────────────────
bool Catalog::seen_project(const ProjectRow& r) {
    if (!m_db || !m_rw || r.id.empty()) return false;
    const char* sql =
        "INSERT INTO projects (id, path, name, tmpl, seen, gone) VALUES (?,?,?,?,?,0) "
        "ON CONFLICT(id) DO UPDATE SET path = excluded.path, name = excluded.name, tmpl = excluded.tmpl, "
        "seen = excluded.seen, gone = 0";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, r.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, r.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, r.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, r.tmpl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, r.seen);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool Catalog::set_status(const std::string& id, const std::string& status, const std::string& outcome) {
    if (!m_db || !m_rw) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE projects SET status = ?, outcome = ? WHERE id = ?", -1, &st, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, outcome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, id.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(m_db) == 1;
    sqlite3_finalize(st);
    return ok;
}

namespace {
std::vector<ProjectRow> project_query(sqlite3* db, const char* where, const std::string& arg) {
    std::vector<ProjectRow> out;
    if (!db) return out;
    const std::string sql = std::string("SELECT id, path, name, tmpl, status, outcome, seen, gone FROM projects ") + where;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;   // no table yet: none
    if (!arg.empty()) sqlite3_bind_text(st, 1, arg.c_str(), -1, SQLITE_TRANSIENT);
    const auto txt = [&](int c) { const auto* p = sqlite3_column_text(st, c); return p ? std::string(reinterpret_cast<const char*>(p)) : std::string(); };
    while (sqlite3_step(st) == SQLITE_ROW) {
        ProjectRow r;
        r.id = txt(0); r.path = txt(1); r.name = txt(2); r.tmpl = txt(3);
        r.status = txt(4); r.outcome = txt(5);
        r.seen = sqlite3_column_int64(st, 6);
        r.gone = sqlite3_column_int(st, 7) != 0;
        out.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    return out;
}
}  // namespace

std::optional<ProjectRow> Catalog::project(const std::string& id) const {
    auto v = project_query(m_db, "WHERE id = ?", id);
    if (v.empty()) return std::nullopt;
    return v.front();
}

std::vector<ProjectRow> Catalog::projects() const { return project_query(m_db, "ORDER BY path", ""); }

bool Catalog::mark_project_gone(const std::string& id, std::int64_t when) {
    if (!m_db || !m_rw) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE projects SET gone = 1, seen = ? WHERE id = ? AND gone = 0", -1, &st, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, when);
    sqlite3_bind_text(st, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(m_db) == 1;
    sqlite3_finalize(st);
    return ok;
}

// ── Uses (s010g) ─────────────────────────────────────────────────────────────
namespace {
bool use_exec(sqlite3* db, const char* sql, const std::string& a, const std::string& b) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, a.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, b.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) == 1;
    sqlite3_finalize(st);
    return ok;
}
}  // namespace

bool Catalog::add_use(const std::string& project, const std::string& used) {
    if (!m_db || !m_rw || project.empty() || used.empty()) return false;
    return use_exec(m_db, "INSERT OR IGNORE INTO uses (project, used) VALUES (?, ?)", project, used);
}

bool Catalog::drop_use(const std::string& project, const std::string& used) {
    if (!m_db || !m_rw) return false;
    return use_exec(m_db, "DELETE FROM uses WHERE project = ? AND used = ?", project, used);
}

std::vector<std::pair<std::string, std::string>> Catalog::uses() const {
    std::vector<std::pair<std::string, std::string>> out;
    if (!m_db) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT project, used FROM uses ORDER BY project, used", -1, &st, nullptr) != SQLITE_OK)
        return out;                                                   // no table yet (a v2 catalog read-only): none
    while (sqlite3_step(st) == SQLITE_ROW) out.emplace_back(text(st, 0), text(st, 1));
    sqlite3_finalize(st);
    return out;
}

// ── Archive (s013a) ──────────────────────────────────────────────────────────
namespace {
constexpr const char* kArcCols = "id, path, origin, sha256, size, archived, batch, media, proved";
Archived arc_row(sqlite3_stmt* st) {
    Archived a;
    a.id = text(st, 0);
    a.path = text(st, 1);
    a.origin = text(st, 2);
    a.sha256 = text(st, 3);
    a.size = u64(sqlite3_column_int64(st, 4));
    a.when = sqlite3_column_int64(st, 5);
    a.batch = u64(sqlite3_column_int64(st, 6));
    a.media = text(st, 7);
    a.proved = sqlite3_column_int64(st, 8);
    return a;
}
}  // namespace

bool Catalog::put_archived(const Archived& a) {
    if (!m_db || !m_rw || a.id.empty()) return false;
    const std::string sql = std::string("INSERT OR REPLACE INTO archived (") + kArcCols + ") VALUES (?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, a.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, a.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, a.origin.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, a.sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, s64(a.size));
    sqlite3_bind_int64(st, 6, a.when);
    sqlite3_bind_int64(st, 7, s64(a.batch));
    sqlite3_bind_text(st, 8, a.media.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 9, a.proved);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

std::vector<Archived> Catalog::archived() const {
    std::vector<Archived> out;
    if (!m_db) return out;
    const std::string sql = std::string("SELECT ") + kArcCols + " FROM archived ORDER BY path";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(arc_row(st));
    sqlite3_finalize(st);
    return out;
}

std::optional<Archived> Catalog::archived_by_id(const std::string& id) const {
    if (!m_db) return std::nullopt;
    const std::string sql = std::string("SELECT ") + kArcCols + " FROM archived WHERE id = ?";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Archived> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = arc_row(st);
    sqlite3_finalize(st);
    return out;
}

std::size_t Catalog::drop_archived_batch(std::uint64_t batch) {
    if (!m_db || !m_rw || !batch) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM archived WHERE batch = ?", -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, s64(batch));
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? static_cast<std::size_t>(sqlite3_changes(m_db)) : 0;
}

// ── Media copies (s013b) ─────────────────────────────────────────────────────
namespace {
constexpr const char* kCopyCols = "id, media, path, sha256, size, proved, batch";
Copy copy_row(sqlite3_stmt* st) {
    Copy c;
    c.id = text(st, 0);
    c.media = text(st, 1);
    c.path = text(st, 2);
    c.sha256 = text(st, 3);
    c.size = u64(sqlite3_column_int64(st, 4));
    c.proved = sqlite3_column_int64(st, 5);
    c.batch = u64(sqlite3_column_int64(st, 6));
    return c;
}
}  // namespace

bool Catalog::put_copy(const Copy& c) {
    if (!m_db || !m_rw || c.id.empty() || c.media.empty()) return false;
    const std::string sql = std::string("INSERT OR REPLACE INTO copies (") + kCopyCols + ") VALUES (?,?,?,?,?,?,?)";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, c.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, c.media.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, c.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, c.sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, s64(c.size));
    sqlite3_bind_int64(st, 6, c.proved);
    sqlite3_bind_int64(st, 7, s64(c.batch));
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

std::vector<Copy> Catalog::copies() const {
    std::vector<Copy> out;
    if (!m_db) return out;
    const std::string sql = std::string("SELECT ") + kCopyCols + " FROM copies ORDER BY media, path";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(copy_row(st));
    sqlite3_finalize(st);
    return out;
}

std::vector<Copy> Catalog::copies_of(const std::string& id) const {
    std::vector<Copy> out;
    if (!m_db) return out;
    const std::string sql = std::string("SELECT ") + kCopyCols + " FROM copies WHERE id = ? ORDER BY media";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(copy_row(st));
    sqlite3_finalize(st);
    return out;
}

std::size_t Catalog::drop_copies_batch(std::uint64_t batch) {
    if (!m_db || !m_rw || !batch) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM copies WHERE batch = ?", -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, s64(batch));
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? static_cast<std::size_t>(sqlite3_changes(m_db)) : 0;
}

}  // namespace tidy::core::catalog
