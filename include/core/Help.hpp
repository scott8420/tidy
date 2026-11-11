#pragma once
// core::help -- "How to use Tidy" (s017, Scott: "i need a help dlg and a
// process to use"). GTK-free: the text lives here so the selftest can check
// every page it sends you to exists, and every page is covered.
//
// The process, in the order you meet it: set up once; day to day; when Tidy
// says something needs attention; the archive cycle; the way back. Then the
// retention levels in one line each.
#include <string>
#include <vector>

namespace tidy::core::help {

struct Step {
    std::string text;      // what to do
    std::string page;      // the page it happens on ("" = none -- a menu or the shell)
};
struct Section {
    std::string title, intro;
    std::vector<Step> steps;
};

const std::vector<Section>& sections();
// (title, one line) per retention level, from core::retention.
std::vector<std::pair<std::string, std::string>> levels();

}  // namespace tidy::core::help
