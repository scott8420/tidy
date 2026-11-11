#pragma once
// core::help -- "How to use Tidy" (s017; s017a: Scott, "less top down, with
// topics and instructions, and an introduction to the tool"). GTK-free: the
// text lives here so the selftest can check every page has a topic and every
// page a topic names exists.
//
// A topic = what it is (a few short paragraphs) + how to (numbered steps) +
// the page it lives on (a "Show" button). The window lists the topics on the
// left, grouped; the Introduction opens first.
#include <string>
#include <vector>

namespace tidy::core::help {

struct Topic {
    std::string id, group, title;
    std::vector<std::string> about;   // paragraphs: what it is, why
    std::vector<std::string> steps;   // how to, in order ("" = none)
    std::string page;                 // the page it lives on ("" = none)
};

const std::vector<Topic>& topics();   // in the order the window lists them; the first is the Introduction
const Topic* find(const std::string& id);

}  // namespace tidy::core::help
