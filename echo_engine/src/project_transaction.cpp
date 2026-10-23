#include <windows.h>
#include "echo/project_transaction.hpp"
#include "echo/resources.hpp"
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
namespace echo {
namespace {
struct CaseInsensitive {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) ==
               CSTR_LESS_THAN;
    }
};
bool protectedTransactionPath(std::string_view name) {
    auto path = pathFromUtf8(name).generic_wstring();
    return CompareStringOrdinal(path.c_str(), int(path.size()), L".echo/save.lock", 15, TRUE) ==
               CSTR_EQUAL ||
           CompareStringOrdinal(path.c_str(), int(path.size()), L".echo/save-transaction.echo", 27,
                                TRUE) == CSTR_EQUAL;
}
bool recoveryPath(std::string_view name) {
    auto path = pathFromUtf8(name);
    if (path.empty())
        return false;
    auto first = path.begin()->native();
    return CompareStringOrdinal(first.c_str(), int(first.size()), L".echo", 5, TRUE) == CSTR_EQUAL;
}
std::filesystem::path checkedPath(const std::filesystem::path& root, std::string_view name) {
    if (name.empty() || name.size() > 2048 || name.find(':') != std::string_view::npos ||
        name.find('\0') != std::string_view::npos)
        throw std::runtime_error("Invalid transaction path");
    auto relative = pathFromUtf8(name);
    if (relative.is_absolute() || relative.has_root_name())
        throw std::runtime_error("Transaction paths must be relative");
    for (const auto& part : relative) {
        auto value = part.native();
        if (part == "." || part == ".." || value.empty() || value.back() == L'.' || value.back() == L' ')
            throw std::runtime_error("Transaction path traversal / Windows alias rejected");
    }
    auto path = root / relative;
    auto canonical = std::filesystem::weakly_canonical(path).lexically_relative(
        std::filesystem::weakly_canonical(root));
    if (canonical.empty() || *canonical.begin() == ".." || std::filesystem::is_symlink(path))
        throw std::runtime_error("Transaction path escaped project");
    return path;
}
std::string readText(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    auto length = f.tellg();
    if (!f || length < 0 || std::uint64_t(length) > projectTransactionBudget)
        throw std::runtime_error("Project transaction read exceeds 64 MiB");
    std::string text(std::size_t(length), '\0');
    f.seekg(0);
    f.read(text.data(), length);
    if (!f)
        throw std::runtime_error("Project transaction read failed");
    return text;
}
struct Lock {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit Lock(const std::filesystem::path& root) {
        auto folder = checkedPath(root, ".echo");
        std::filesystem::create_directories(folder);
        auto path = checkedPath(root, ".echo/save.lock");
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Another editor is saving this project");
    }
    ~Lock() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};
struct Entry {
    std::string name, before;
    bool exists{};
};
struct Transaction {
    std::vector<Entry> files;
    std::vector<ProjectFileMove> moves;
};
Transaction readJournal(const std::filesystem::path& root, std::string_view data) {
    std::istringstream in{std::string(data)};
    std::string header;
    int count, moveCount = 0;
    if (!(in >> header >> count) || (header != "ECHO_TRANSACTION_1" && header != "ECHO_TRANSACTION_2") ||
        count < 0 || count > 128)
        throw std::runtime_error("Invalid save transaction journal");
    if (header == "ECHO_TRANSACTION_2" && (!(in >> moveCount) || moveCount < 0 || moveCount > 4096))
        throw std::runtime_error("Invalid save move count");
    if (!count && !moveCount)
        throw std::runtime_error("Empty save transaction");
    Transaction t;
    std::set<std::wstring, CaseInsensitive> names, sources, destinations;
    for (int i = 0; i < count; ++i) {
        Entry e;
        int existed = -1;
        if (!(in >> std::quoted(e.name) >> existed >> std::quoted(e.before)) ||
            (existed != 0 && existed != 1) || !names.insert(checkedPath(root, e.name).native()).second ||
            recoveryPath(e.name))
            throw std::runtime_error("Invalid save transaction entry");
        checkedPath(root, e.name);
        e.exists = existed != 0;
        t.files.push_back(std::move(e));
    }
    for (int i = 0; i < moveCount; ++i) {
        ProjectFileMove m;
        if (!(in >> std::quoted(m.from) >> std::quoted(m.to)) || m.from == m.to ||
            protectedTransactionPath(m.from) || protectedTransactionPath(m.to) ||
            names.contains(checkedPath(root, m.from).native()) ||
            names.contains(checkedPath(root, m.to).native()) ||
            !sources.insert(checkedPath(root, m.from).native()).second ||
            !destinations.insert(checkedPath(root, m.to).native()).second)
            throw std::runtime_error("Invalid save transaction move");
        checkedPath(root, m.from);
        checkedPath(root, m.to);
        t.moves.push_back(std::move(m));
    }
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Trailing save transaction data");
    return t;
}
bool recover(const std::filesystem::path& root) {
    auto journal = checkedPath(root, ".echo/save-transaction.echo");
    if (!std::filesystem::exists(journal))
        return false;
    auto t = readJournal(root, readText(journal));
    // Roll back asset chains in reverse: replacement -> staging, backup -> original.
    for (auto it = t.moves.rbegin(); it != t.moves.rend(); ++it) {
        auto source = checkedPath(root, it->from), dest = checkedPath(root, it->to);
        if (!std::filesystem::exists(source) && std::filesystem::exists(dest)) {
            std::filesystem::create_directories(source.parent_path());
            std::filesystem::rename(dest, source);
        }
    }
    // Idempotent rollback: keep the journal until EVERY original is restored.
    for (const auto& e : t.files) {
        auto path = checkedPath(root, e.name);
        if (e.exists) {
            if (std::filesystem::exists(path) && readText(path) == e.before)
                continue;
            std::filesystem::create_directories(path.parent_path());
            writeTextAtomically(path, e.before);
        } else if (std::filesystem::exists(path))
            std::filesystem::remove(path);
    }
    std::filesystem::remove(journal);
    return true;
}
} // namespace
bool recoverProjectTransaction(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root))
        return false;
    Lock lock(root);
    return recover(root);
}
void commitProjectFiles(const std::filesystem::path& root,
                        const std::map<std::string, std::string>& files,
                        const std::vector<ProjectFileMove>& moves) {
    if (files.empty() && moves.empty())
        return;
    if (files.size() > 128 || moves.size() > 4096)
        throw std::runtime_error("Project transaction capacity exceeded");
    Lock lock(root);
    recover(root);
    std::ostringstream journal;
    journal << "ECHO_TRANSACTION_2 " << files.size() << ' ' << moves.size() << '\n';
    std::size_t bytes = 0;
    for (const auto& [name, text] : files) {
        if (recoveryPath(name))
            throw std::runtime_error("Recovery files cannot be transaction payloads");
        auto path = checkedPath(root, name);
        bool exists = std::filesystem::exists(path);
        auto before = exists ? readText(path) : std::string{};
        bytes += before.size() + text.size() + name.size();
        if (bytes > projectTransactionBudget)
            throw std::runtime_error("Project save exceeds 64 MiB transaction budget");
        journal << std::quoted(name) << ' ' << exists << ' ' << std::quoted(before) << '\n';
    }
    std::set<std::filesystem::path> vacated;
    for (const auto& m : moves) {
        auto from = checkedPath(root, m.from), to = checkedPath(root, m.to);
        if (!std::filesystem::is_regular_file(from) && !vacated.contains(from))
            throw std::runtime_error("Save move source is missing");
        if (std::filesystem::exists(to) && !vacated.contains(to))
            throw std::runtime_error("Save move destination exists");
        vacated.insert(from);
        vacated.erase(to);
        journal << std::quoted(m.from) << ' ' << std::quoted(m.to) << '\n';
    }
    auto data = journal.str();
    if (data.size() > projectTransactionBudget)
        throw std::runtime_error("Save rollback journal exceeds 64 MiB");
    readJournal(root, data); // Validate the entire plan before the first filesystem mutation.
    auto path = checkedPath(root, ".echo/save-transaction.echo");
    writeTextAtomically(path, data);
    try {
        for (const auto& m : moves) {
            auto source = checkedPath(root, m.from), dest = checkedPath(root, m.to);
            std::filesystem::create_directories(dest.parent_path());
            std::filesystem::rename(source, dest);
        }
        for (const auto& [name, text] : files) {
            auto target = checkedPath(root, name);
            std::filesystem::create_directories(target.parent_path());
            writeTextAtomically(target, text);
        }
        // Removing the journal is the commit point. Before it, startup restores originals.
        std::filesystem::remove(path);
    } catch (...) {
        recover(root);
        throw;
    }
}
} // namespace echo
