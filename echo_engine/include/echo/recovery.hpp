#pragma once
#include "database.hpp"
#include "events.hpp"
#include "resources.hpp"
namespace echo {
inline constexpr std::size_t recoveryBudget = 32ull * 1024 * 1024;
struct Recovery {
    std::string map;
    std::map<std::string, std::string> files;
};
std::string saveRecovery(const Recovery&);
Recovery loadRecovery(std::string_view);
void validateRecovery(const Recovery&, const Resources&);
struct ProjectIssue {
    bool error{};
    std::string location, message;
};
std::vector<ProjectIssue> inspectProject(const Resources&);
} // namespace echo
