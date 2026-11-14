#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::guide -- s030a the guided Start (Scott, 2026-11-13 15:22-15:26: "I just
// need... Do this, do this now"; "a set of icons with arrow ... Step 1: set
// basics -> Step 2: set bundles -> etc"; "an expert mode too").
//
// Six steps, in the order Tidy reads a home (each level claims its files
// before the next, so the order matters):
//
//   1 Basics -> 2 Bundles -> 3 Projects -> 4 Sets -> 5 Loose files -> 6 Closet
//
// Each step is worked out from ONE read-only recognise report of the whole
// home: how many things it has to do, and the ONE thing to do now (a sentence
// and what Do it opens). A step with nothing to do is done; the first step not
// done (and not skipped) is "now". Sets is not ready yet (comics, the EBooks
// library: s030b / s030c) -- it says so and never blocks the road.
//
// Expert mode (Tidy's own <data>/mode.txt): the tabs as they were. Guided (the
// default) hides the tab rows; Start's step strip is the way in.
//
// GTK-free. READ-ONLY except mode.txt (through core::own).
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Recognise.hpp"

#include <set>
#include <string>
#include <vector>

namespace tidy::core::guide {

enum class Id { Basics = 0, Bundles, Projects, Sets, Loose, Closet };
enum class State { Done, Now, Todo, NotReady, Skipped };

// What Do it does -- the page acts; the guide only says which and on what.
enum class Act {
    None,
    Quarantine,      // open Review -> Quarantine
    HomeDesign,      // open Design -> Types (Home first)
    SendHome,        // send `bundles` home: one preview, one Do it, one undo
    OrganizeAs,      // the Organize window on `folder` with `type` picked
    SortFolder,      // Sort, for `folder` only (`whole` left out)
    Closet,          // open the Closet
    Reshelve,        // s030e: the comics off the shelf -> their series (one preview, one undo)
};

struct Step {
    Id id = Id::Basics;
    std::string title;          // "Bundles"
    std::string icon;           // a symbolic icon name
    State state = State::Todo;
    std::size_t todo = 0;       // things left in this step
    std::string now;            // "Send 7 of your app files to their homes."
    std::string after;          // "3 more after this." / ""
    Act act = Act::None;
    std::string folder, type;   // OrganizeAs / SortFolder: relative to home
    std::vector<std::string> bundles;   // SendHome: rels
    std::vector<std::string> homes;     //   ... and each one's home (same order)
    std::vector<std::string> whole;     // SortFolder: projects + bundles left out
    std::vector<std::pair<std::string, std::string>> items;   // OrganizeAs: every (folder, type), in order (s030c)
    std::vector<std::string> folders;   // SortFolder: every folder, biggest first (s030d)
    std::string details;        // the Expert page id for "Show me the details"
};

struct Inputs {
    std::size_t held = 0;          // Quarantine: folders held, waiting for an answer
    bool home_saved = false;       // the Home design saved once (looked over)
    std::set<int> skipped;         // steps the user skipped this run (Id as int)
    std::size_t comics = 0;        // s030e: comics off the Comics shelf
    std::size_t series = 0;        //   ... in how many series
    std::size_t movies = 0;        // s030j: movies off the Movies shelf (folders or files)
    std::size_t episodes = 0;      // s030j: season folders + loose episodes off the Series shelf
    // s030l (Scott: "the screen says we have 8 files ... and there are none when i say do it"):
    // the Closet step counts what the Closet PAGE shows (closet::view total); -1 = not given.
    long long closet = -1;
    std::size_t books = 0;         // s030m: books in the old EBooks library (Documents/EBooks) off the shelf
    std::set<std::string> settled; // s030f: folders Sort found nothing to move in (Tidy's own settled.txt)
};

// The six steps from a whole-home report (pure).
std::vector<Step> steps(const recognise::Report& r, const Inputs& in);
// The step to show: the first Now; else the first Todo; else -1 (all done).
int current(const std::vector<Step>& v);
const char* state_text(State s);   // "done", "do this now", "to do", "not ready yet", "skipped"

// s030f (Scott: "When I get to this screen do it does nothing"): a folder Sort
// looked in from Start and found nothing to move is SETTLED -- Loose files does
// not offer it again. Tidy's own <data>/settled.txt, one folder (rel) a line.
std::set<std::string> settled(const std::string& data_dir);
std::string settle(const std::string& data_dir, const std::string& rel);   // "" = saved

// Expert mode: <data>/mode.txt says "expert". Guided otherwise.
bool expert(const std::string& data_dir);
std::string set_expert(const std::string& data_dir, bool on);   // "" = saved, else why not

}  // namespace tidy::core::guide
