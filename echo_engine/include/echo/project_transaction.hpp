#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace echo {
// Authoring transactions only. WorldState memory is never used as a save format.
inline constexpr std::size_t projectTransactionBudget = 64ull * 1024 * 1024;
bool recoverProjectTransaction(const std::filesystem::path& root);
struct ProjectFileMove {
    std::string from, to;
};
void commitProjectFiles(const std::filesystem::path& root,
                        const std::map<std::string, std::string>& files,
                        const std::vector<ProjectFileMove>& moves = {});
} // namespace echo
