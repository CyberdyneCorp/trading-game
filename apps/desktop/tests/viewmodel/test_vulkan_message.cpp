#include <doctest/doctest.h>

#include "viewmodel/vulkan_message.hpp"

using namespace tradelab::desktop;

TEST_CASE("vulkan message: PT-BR by default with the Linux fix and the library detail") {
  Catalog c = builtin_catalog();
  const AlertText a = vulkan_missing_message(c, HostOs::Linux, "R_ERROR_BACKEND: no libvulkan.so.1");
  CHECK(a.title == "TradeLab: driver Vulkan não encontrado");
  CHECK(a.body.find("O TradeLab precisa de um driver Vulkan") == 0);
  CHECK(a.body.find("Instale `libvulkan1`") != std::string::npos);
  CHECK(a.body.find("Detalhe: R_ERROR_BACKEND: no libvulkan.so.1") != std::string::npos);
}

TEST_CASE("vulkan message: en, macOS fix, no empty detail line") {
  Catalog c = builtin_catalog();
  c.set_locale(Locale::En);
  const AlertText a = vulkan_missing_message(c, HostOs::MacOS, "");
  CHECK(a.body.find("TradeLab needs a Vulkan driver") == 0);
  CHECK(a.body.find("download TradeLab again") != std::string::npos);
  CHECK(a.body.find("Detail") == std::string::npos);
}
