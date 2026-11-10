#pragma once
// core::pages -- what each of Tidy's pages promises, as data (s003, the spine).
//
// The spine shows every page on the road as a hero with its promise and the
// milestone that brings it to life. One table, so the sidebar (core::look::
// tidy_nav) and the pages can't drift: the selftest proves every nav item has
// its text here and every text has its nav item.
#include <string>
#include <vector>

namespace tidy::core::pages {

struct Page {
    std::string id;          // == the nav item's id == the stack child name
    std::string title;       // the hero title
    std::string promise;     // one line: what the page will do
    std::string milestone;   // "s006" -- the road row that brings it to life
};

const std::vector<Page>& all();
const Page* find(const std::string& id);

}  // namespace tidy::core::pages
