#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::shape -- s023i: a template made from a scan (Scott, 2026-11-11, 19:57:
// "I think a scan gives all the file types and folders, then I add new folders
// and set file types to it. I also define file types at top level that are the
// project that builds other files. Then it has a name and category and where
// the template is organized to.")
//
//   scan()   READ-ONLY: one project folder or a folder of many -> every file
//            TYPE with counts, and every FOLDER (two deep) with counts.
//            A numbered run (A0001.png .. A1949.png -- Scott: "archive images
//            are sequencial") is a row of its own, apart from the other files
//            of that type; a run = 5+ files of one type whose names are the
//            same prefix + the same number of digits.
//   make()   the rows, each given a folder (or the project's top, or left
//            alone), + the kept folders + name + category -> a template whose
//            routes Conform (s023h) follows. Source types (the project files
//            that build the others: .optik, .quire) -> the project's top.
//
// Same walk as Conform: hidden names and symlinks skipped, a package is one
// item, a nested -project / git repo / protected folder is left whole.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Templates.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tidy::core::shape {

inline constexpr std::size_t kRunMin = 5;   // files in a numbered run

struct Row {
    std::string ext;                 // ".png" (lower case); "" = no extension
    std::string prefix;              // a run's prefix ("A"); "" for the others
    int width = 0;                   // a run's digits (4 for A0001); 0 = not a run
    std::size_t count = 0;
    std::string first, last;         // a run: A0001.png .. A1949.png; else one example name
    std::vector<std::pair<std::string, std::size_t>> where;   // folder (two deep, "" = the top) -> files, most first
    std::size_t projects = 0;        // in how many of the scanned folders

    bool run() const { return width > 0; }
    bool routable() const { return !ext.empty(); }   // a glob can't say "no dot"
    std::string key() const;         // stable: ".png", ".png#A#4"
    std::string pattern() const;     // "*.png" or "A[0-9][0-9][0-9][0-9].png"
    std::string title() const;       // ".png" / ".png numbered A0001 .. A1949"
    std::string mostly() const;      // the folder most of it is in now ("" = the top), or "" if none holds half
};

struct FolderSeen {
    std::string path;                // "Archive", "Workups/Images"
    std::size_t files = 0;           // directly in it
    std::size_t projects = 0;        // in how many of the scanned folders
};

struct Scan {
    std::vector<std::string> dirs;   // the project folders read (absolute)
    std::vector<Row> rows;           // by type, the runs before the others, most files first within a type
    std::vector<FolderSeen> folders; // sorted by path
    std::size_t files = 0;
    std::size_t whole = 0;           // nested -projects / repos / protected, left whole
};

// The runs of numbered names: "A0001" -> {"A", 4}; "Clock" -> {"", 0}.
std::pair<std::string, int> numbered(const std::string& stem);

Scan scan(const std::vector<std::string>& project_dirs);

// What each row gets: a folder of the template, templates::kTop, or "" (left
// alone -- no route; Conform lists it and leaves it). source = it builds the
// others: the project's top, whatever `to` says.
struct Pick {
    std::string to;
    bool source = false;
};

// The template: folders (each parent added; roles guessed from the names),
// routes in this order -- sources -> (top), the numbered runs, then the rest.
// A row not picked has no route. problems() of the result says what is wrong.
templates::Template make(const std::string& name, const std::string& category, const std::vector<std::string>& folders,
                         const std::vector<Row>& rows, const std::map<std::string, Pick>& picks);

// A template name from a folder name: "EBooks" -> "ebooks-project".
std::string name_from(const std::string& folder_name);

}  // namespace tidy::core::shape
