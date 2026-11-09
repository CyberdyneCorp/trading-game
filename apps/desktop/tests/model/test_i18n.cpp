#include <doctest/doctest.h>

#include "model/engine_run.hpp"
#include "model/i18n.hpp"

using namespace tradelab::desktop;

TEST_CASE("i18n: {name} placeholders like the web's t()") {
  CHECK(fill_placeholders("Barra {n} de {total}", {{"n", "3"}, {"total", "80"}}) == "Barra 3 de 80");
  CHECK(fill_placeholders("{a}{a}", {{"a", "x"}}) == "xx");
  CHECK(fill_placeholders("keep {unknown}", {}) == "keep {unknown}");
  CHECK(fill_placeholders("open { brace", {{"x", "1"}}) == "open { brace");
  CHECK(fill_placeholders("", {}).empty());
}

TEST_CASE("i18n: Raven locale CSV with quotes, commas, newlines and comments") {
  const auto rows = parse_csv("\xEF\xBB\xBF# comment\nkey,pt-BR,en\na,\"x, y\",\"say \"\"hi\"\"\"\nb,\"two\nlines\",z\r\n");
  REQUIRE(rows.size() == 3);
  CHECK(rows[1] == std::vector<std::string>{"a", "x, y", "say \"hi\""});
  CHECK(rows[2] == std::vector<std::string>{"b", "two\nlines", "z"});
}

TEST_CASE("i18n: catalog lookup, fallback and locale") {
  Catalog c;
  REQUIRE(c.load_csv("key,pt-BR,en\ngreet,Olá {name},Hello {name}\nonly.pt,Só,\n"));
  CHECK(c.tr("greet", {{"name", "Ana"}}) == "Olá Ana");
  c.set_locale(Locale::En);
  CHECK(c.tr("greet", {{"name", "Ana"}}) == "Hello Ana");
  CHECK(c.tr("only.pt") == "Só");
  CHECK(c.tr("no.such.key") == "no.such.key");
  CHECK_FALSE(c.load_csv("id,en\nx,y\n"));
}

TEST_CASE("i18n: system language picks en only for English") {
  CHECK(locale_from_system("en") == Locale::En);
  CHECK(locale_from_system("en-US") == Locale::En);
  CHECK(locale_from_system("en_GB.UTF-8") == Locale::En);
  CHECK(locale_from_system("pt-BR") == Locale::PtBR);
  CHECK(locale_from_system("fr_FR.UTF-8") == Locale::PtBR);
  CHECK(locale_from_system("") == Locale::PtBR);
  CHECK(locale_from_system("eng") == Locale::PtBR);
  CHECK(locale_from_system("C") == Locale::PtBR);
}

TEST_CASE("i18n: the built-in catalog is the generated web catalog plus desktop keys") {
  Catalog c = builtin_catalog();
  CHECK(c.size() > 900);
  CHECK(c.tr("menu.start") == "Iniciar missão");
  CHECK(c.tr("mission.m01-false-breakout.title") == "Missão 01 · Range");
  CHECK(c.has("desktop.vulkanMissing.body"));
  c.set_locale(Locale::En);
  CHECK(c.tr("menu.start") == "Start mission");
  CHECK(c.tr("rail.fundingIn", {{"n", "3"}}) == "in 3 bars");
}

// Regression: missions.ts adds m04..m12 with `texts['id'] = {...}` statements, which the generator
// first skipped, so the menu showed raw keys for them.
TEST_CASE("i18n: every engine mission has its web title, tagline and topic in both languages") {
  Catalog c = builtin_catalog();
  const auto missions = missions_only(engine_missions());
  REQUIRE(missions.size() == 25);
  for (const auto& m : missions) {
    for (const char* field : {"title", "tagline", "topic", "reveal"}) {
      const std::string key = "mission." + m.id + "." + field;
      INFO(key);
      CHECK(c.has(key));
      c.set_locale(Locale::En);
      CHECK(c.tr(key) != key);
      c.set_locale(Locale::PtBR);
    }
  }
}
