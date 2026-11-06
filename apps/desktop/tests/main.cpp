#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

// Every test case starts with empty saved data: TRADELAB_DATA_DIR (the prefs file of App) points to a
// folder of this process that is emptied first, so a key remapped or a run settled by one test (or by
// an earlier run of the suite) never reaches another.
class FreshDataDir final : public doctest::IReporter {
 public:
  explicit FreshDataDir(const doctest::ContextOptions& /*options*/) {}

  void test_case_start(const doctest::TestCaseData& /*data*/) override {
    namespace fs = std::filesystem;
    static const fs::path dir = fs::temp_directory_path() / ("tradelab-test-data-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    ::setenv("TRADELAB_DATA_DIR", dir.c_str(), 1);
  }

  void report_query(const doctest::QueryData& /*data*/) override {}
  void test_run_start() override {}
  void test_run_end(const doctest::TestRunStats& /*stats*/) override {}
  void test_case_reenter(const doctest::TestCaseData& /*data*/) override {}
  void test_case_end(const doctest::CurrentTestCaseStats& /*stats*/) override {}
  void test_case_exception(const doctest::TestCaseException& /*e*/) override {}
  void subcase_start(const doctest::SubcaseSignature& /*signature*/) override {}
  void subcase_end() override {}
  void log_assert(const doctest::AssertData& /*data*/) override {}
  void log_message(const doctest::MessageData& /*data*/) override {}
  void test_case_skipped(const doctest::TestCaseData& /*data*/) override {}
};

}  // namespace

REGISTER_LISTENER("fresh-data-dir", 1, FreshDataDir);
