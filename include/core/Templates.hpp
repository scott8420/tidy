#pragma once
// core::templates -- a project's upper levels, as data (s007; REQUIREMENTS §5).
// GTK-free and pure, apart from two READ-ONLY calls (lift's folder scan and
// list()'s directory read). Written only by core::own, under
// <data>/templates/<name>.template.
//
//   A template carries: name + category (writing-project -> Writing); the tree,
//   upper levels only (two deep -- below that a project is free); each folder's
//   role (product / input / history -> its default retention at close); type
//   routing (*.curvz -> Graphics).
//
//   Lift from an existing project = a PROPOSAL: every lifted folder arrives
//   unconfirmed, and Save is refused until each is kept (confirmed) or removed.
//   A blind copy turns an example's drift into the rule (REQUIREMENTS §5).
//
//   Drift check: a project against its template at template depth only. A
//   folder's children are checked only where the template names children for
//   it (Research/ may grow forty sub-folders and Tidy does not care).
//
//   Undo of a template edit lives in the Builder (Scott, s006a), not Activity:
//   every edit is a state, Undo goes back one.
#include <set>
#include <string>
#include <vector>

namespace tidy::core::templates {

inline constexpr const char* kDir = "templates";       // under the data folder
inline constexpr const char* kExt = ".template";
inline constexpr int kDepth = 2;                        // "upper levels only (about two deep)"

enum class Role { Product, Input, History };
const char* id_of(Role r);                // "product" | "input" | "history" -- on disk, never changes
const char* title_of(Role r);             // "Product" ...
const char* meaning_of(Role r);           // what happens at close
bool role_of(const std::string& id, Role* out);
const std::vector<Role>& roles();

struct Folder {
    std::string path;                     // "Graphics" or "Graphics/Covers"
    Role role = Role::Input;
    bool operator==(const Folder&) const = default;
};
struct Route {
    std::string pattern;                  // file-name glob, case-insensitive
    std::string folder;                   // a folder of the template
    bool operator==(const Route&) const = default;
};
struct Template {
    std::string name;                     // writing-project
    std::string category;                 // Writing -- the home folder its projects live in
    std::vector<Folder> folders;          // kept sorted by path (parents before children)
    std::vector<Route> routes;            // first fit wins
    bool operator==(const Template&) const = default;
};

// The common shell (as a code workspace): backups/ history; docs/, fixes/,
// scripts/ input. A new template starts with it.
Template shell(const std::string& name = "", const std::string& category = "");

// Names. "" = fine, else why not.
std::string name_refusal(const std::string& name);       // [a-z0-9-], 1..64, starts with a letter or digit
std::string part_refusal(const std::string& part);       // one folder name: not hidden, no '/', not '..'
std::string path_refusal(const std::string& path);       // 1..kDepth parts
// A category: one or two folders under home ("Writing", "Develop/gtk" -- s010c).
std::string category_refusal(const std::string& category);
// Everything wrong with a template, each a sentence ("" list = fine).
std::vector<std::string> problems(const Template& t);

std::string format(const Template& t);
Template parse(const std::string& text);   // bad lines dropped; folders sorted
std::string rel_of(const std::string& name);   // "templates/<name>.template"

// Saved templates, sorted by name (READ-ONLY: a directory listing + reads).
std::vector<Template> list(const std::string& data_dir);
// Write one through core::own. "" = written, else why not (problems included).
std::string save(const std::string& data_dir, const Template& t);

// The first route `file_name` fits -> its folder, or "".
std::string route_for(const Template& t, const std::string& file_name);
const Folder* find(const Template& t, const std::string& path);

// ── Lift and drift ─────────────────────────────────────────────────────────
// The folders of `dir`, kDepth deep, relative and sorted (READ-ONLY: hidden
// names, symlinks and files skipped; nothing else is opened).
std::vector<std::string> folders_of(const std::string& dir);
// A role guessed from a folder's name (backups / Old / archive -> history;
// Final / Release / Export / Out / dist -> product; else input).
Role guess_role(const std::string& path);
// A proposal from a project's folders. name from the folder ("Jasper-project"
// -> "jasper-project"... the builder renames it), category from its parent.
Template lift(const std::string& project_dir, const std::vector<std::string>& folders);

struct Draft;
// lift + folders_of as a builder draft: every folder unconfirmed (READ-ONLY).
Draft proposal(const std::string& project_dir);

struct Drift {
    enum class Kind { Missing, Extra } kind;
    std::string path;
    bool operator==(const Drift&) const = default;
};
// A project's folders (folders_of) against its template, at template depth.
std::vector<Drift> drift(const Template& t, const std::vector<std::string>& folders);

// ── The builder: a draft and its undo ──────────────────────────────────────
struct Draft {
    Template t;
    std::set<std::string> unconfirmed;    // lifted folders not yet kept
    std::string origin;                   // the lifted project, or ""
    bool operator==(const Draft&) const = default;
};

class Builder {
public:
    Builder() = default;
    explicit Builder(Draft d, std::string saved_as = "")
        : m_now(std::move(d)), m_saved_as(std::move(saved_as)) {}

    const Draft& draft() const { return m_now; }
    const Template& tpl() const { return m_now.t; }
    const std::string& saved_as() const { return m_saved_as; }   // the name on disk, "" = new
    bool can_undo() const { return !m_past.empty(); }
    std::size_t edits() const { return m_past.size(); }

    // Each edit: "" = done (and undoable), else why not (nothing changed).
    std::string set_name(const std::string& name);
    std::string set_category(const std::string& category);
    std::string add_folder(const std::string& path, Role role);
    std::string remove_folder(const std::string& path);   // its children and routes go too
    std::string set_role(const std::string& path, Role role);
    std::string confirm(const std::string& path);          // a lifted folder kept (its parent too)
    std::string add_route(const std::string& pattern, const std::string& folder);
    std::string remove_route(const std::string& pattern);
    std::string adopt(const Drift& d);                     // Extra -> added (input); Missing -> removed
    bool undo();

    // Why Save would be refused ("" = it may be saved). Unconfirmed lifted
    // folders, problems, or a new template over an existing name.
    std::string save_refusal(const std::vector<std::string>& existing_names) const;
    void mark_saved() { m_saved_as = m_now.t.name; }

private:
    std::string apply(Draft next);
    Draft m_now;
    std::vector<Draft> m_past;
    std::string m_saved_as;
};

}  // namespace tidy::core::templates
