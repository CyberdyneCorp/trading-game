// The retro theme's colours (resources/theme/*.theme.xml) meet WCAG 2.2 AA (4.5:1) for every text
// colour they declare on the backgrounds it is drawn on (window, panel, tooltip, accent buttons).
// Disabled text is exempt (WCAG 1.4.3 "inactive components").
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <string>

#include "model/resources.hpp"

using namespace tradelab::desktop;

namespace {

double channel(int v) {
  const double c = v / 255.0;
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const std::string& hex) {
  const int r = std::stoi(hex.substr(1, 2), nullptr, 16);
  const int g = std::stoi(hex.substr(3, 2), nullptr, 16);
  const int b = std::stoi(hex.substr(5, 2), nullptr, 16);
  return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

double contrast(const std::string& a, const std::string& b) {
  const double la = luminance(a);
  const double lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// Colour roles of <Colors> and the Accent of <Theme>.
std::map<std::string, std::string> roles(const std::string& xml) {
  std::map<std::string, std::string> out;
  const std::regex attr(R"rx(\b([A-Za-z]+)="(#[0-9A-Fa-f]{6})")rx");
  const auto colors = xml.find("<Colors");
  const std::string block = xml.substr(colors, xml.find("/>", colors) - colors);
  for (auto it = std::sregex_iterator(block.begin(), block.end(), attr); it != std::sregex_iterator(); ++it) {
    out[(*it)[1]] = (*it)[2];
  }
  std::smatch m;
  if (std::regex_search(xml, m, std::regex(R"rx(<Theme[^>]*Accent="(#[0-9A-Fa-f]{6})")rx"))) out["Accent"] = m[1];
  return out;
}

// Text colours of the label classes (<Style Widget="Label" Class="up" Text="#...">).
std::map<std::string, std::string> label_classes(const std::string& xml) {
  std::map<std::string, std::string> out;
  const std::regex style(R"rx(<Style Widget="Label" Class="([a-z]+)" Text="(#[0-9A-Fa-f]{6})")rx");
  for (auto it = std::sregex_iterator(xml.begin(), xml.end(), style); it != std::sregex_iterator(); ++it) {
    out[(*it)[1]] = (*it)[2];
  }
  return out;
}

}  // namespace

TEST_CASE("theme: every declared text/background pair meets WCAG 2.2 AA") {
  for (const char* path : {"theme/tradelab.theme.xml", "theme/tradelab-cb.theme.xml"}) {
    const std::string xml(resource_text(path));
    REQUIRE_FALSE(xml.empty());
    auto r = roles(xml);
    const auto classes = label_classes(xml);
    CHECK(classes.size() == 7);
    std::vector<std::pair<std::string, std::string>> texts = {{"Text", r["Text"]}, {"TextPlaceholder", r["TextPlaceholder"]}};
    for (const auto& [cls, colour] : classes) texts.emplace_back("Label." + cls, colour);
    for (const auto& bg : {"Window", "Panel", "Control", "Input"}) {
      for (const auto& [name, colour] : texts) {
        INFO(path << ": " << name << " " << colour << " on " << bg << " " << r[bg]);
        CHECK(contrast(colour, r[bg]) >= 4.5);
      }
    }
    INFO(path);
    CHECK(contrast(r["TooltipText"], r["Tooltip"]) >= 4.5);
    CHECK(contrast(r["AccentText"], r["Accent"]) >= 4.5);
    CHECK(contrast(r["Text"], r["Selection"]) >= 4.5);
  }
}

TEST_CASE("theme: the colorblind palette swaps up/down for blue/orange, as the web's data-palette='cb'") {
  const auto normal = label_classes(std::string(resource_text("theme/tradelab.theme.xml")));
  const auto cb = label_classes(std::string(resource_text("theme/tradelab-cb.theme.xml")));
  CHECK(normal.at("up") == "#39FF88");
  CHECK(normal.at("down") == "#FF5F7A");
  CHECK(cb.at("up") == "#4FD8FF");
  CHECK(cb.at("down") == "#FFA64D");
}
