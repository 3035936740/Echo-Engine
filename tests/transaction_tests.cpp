#include <windows.h>
#include "echo/project_transaction.hpp"
#include "echo/resources.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace echo;
static void check(bool v, const char* label) {
    if (!v)
        throw std::runtime_error(label);
}
template <class F> static void rejects(F f, const char* label) {
    bool rejected = false;
    try {
        f();
    } catch (...) {
        rejected = true;
    }
    check(rejected, label);
}
static std::string read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw std::runtime_error("Transaction test requires output directory");
        auto root = std::filesystem::path(argv[1]) /
                    ("transaction-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        commitProjectFiles(root, {{"a.echo", "old A"}, {"b.echo", "old B"}, {"dir/c.echo", "C"}});
        check(read(root / "a.echo") == "old A" && read(root / "dir/c.echo") == "C", "Multifile commit");
        check(!std::filesystem::exists(root / ".echo/save-transaction.echo"),
              "Successful commit clears journal");
        rejects([&] { commitProjectFiles(root, {{"../outside.echo", "bad"}}); },
                "Transaction traversal rejected before mutations");
        writeTextAtomically(root / "asset.old", "asset bytes");
        auto lock = CreateFileW((root / "b.echo").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(lock != INVALID_HANDLE_VALUE, "Failure injection file lock");
        rejects(
            [&] {
                commitProjectFiles(root, {{"a.echo", "new A"}, {"b.echo", "new B"}},
                                   {{"asset.old", "asset.new"}});
            },
            "Mid-commit failure reported");
        CloseHandle(lock);
        check(read(root / "a.echo") == "old A" && read(root / "b.echo") == "old B",
              "Failure rolls back earlier metadata");
        check(std::filesystem::exists(root / "asset.old") &&
                  !std::filesystem::exists(root / "asset.new"),
              "Failure rolls back asset move");
        check(!std::filesystem::exists(root / ".echo/save-transaction.echo"),
              "Rollback journal clears only after originals restored");
        writeTextAtomically(root / ".echo/save-transaction.echo",
                            "ECHO_TRANSACTION_2 2 1\n\"a.echo\" 1 \"old A\"\n\"new.echo\" 0 "
                            "\"\"\n\"asset.old\" \"asset.new\"\n");
        writeTextAtomically(root / "a.echo", "partial");
        writeTextAtomically(root / "new.echo", "partial new");
        std::filesystem::rename(root / "asset.old", root / "asset.new");
        check(recoverProjectTransaction(root), "Interrupted save detected");
        check(read(root / "a.echo") == "old A" && !std::filesystem::exists(root / "new.echo") &&
                  std::filesystem::exists(root / "asset.old"),
              "Startup restores all originals and asset move");
        check(!recoverProjectTransaction(root), "Recovery is idempotent");
        writeTextAtomically(root / "asset-stage", "replacement");
        commitProjectFiles(root, {{"a.echo", "new A"}},
                           {{"asset.old", ".echo/asset-backup"}, {"asset-stage", "asset.old"}});
        check(read(root / "asset.old") == "replacement" &&
                  read(root / ".echo/asset-backup") == "asset bytes",
              "Replacement chain commits metadata and binary names");
        writeTextAtomically(root / ".echo/save-transaction.echo",
                            "ECHO_TRANSACTION_2 1 0\n\"../outside.echo\" 1 \"bad\"\n");
        rejects([&] { recoverProjectTransaction(root); }, "Corrupt journal path cannot escape project");
        check(read(root / "a.echo") == "new A", "Invalid recovery leaves project unchanged");
        rejects([&] { commitProjectFiles(root, {{"a.echo", "x"}, {"A.ECHO", "y"}}); },
                "Case aliases cannot duplicate one transaction target");
        rejects([&] { commitProjectFiles(root, {{".ECHO/save-transaction.echo", "x"}}); },
                "Case aliases cannot overwrite recovery journal");
        std::filesystem::remove(root / ".echo/save-transaction.echo");
        rejects([&] { commitProjectFiles(root, {{"a.echo", "x"}}, {{".echo/save.lock", "asset.new"}}); },
                "File move cannot capture transaction lock");
        rejects(
            [&] {
                commitProjectFiles(root, {{"a.echo", "x"}},
                                   {{"asset.old", ".ECHO/SAVE-TRANSACTION.ECHO"}});
            },
            "File move cannot replace transaction journal");
        std::cout << "PASS multi-file save, write failure rollback, asset move/replacement, "
                     "interrupted-save startup recovery and path validation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
