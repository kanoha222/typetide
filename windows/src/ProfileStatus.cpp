#include "ProfileStatus.h"

// RED scaffold: every function returns a placeholder until the implementation lands.
namespace profilestatus {

Route RouteOf(const TranslationProfile&, const execution::ConnectionSnapshot&) { return Route::Local; }

bool IsInstalled(const std::vector<std::wstring>&, const std::string&) { return false; }

std::vector<EntryStatus> Describe(const ProfileConfiguration&, const execution::ConnectionSnapshot&,
                                  const std::vector<std::wstring>*) {
    return {};
}

std::vector<Probe> BoundProbes(const ProfileConfiguration&) { return {}; }

std::vector<std::string> PreloadModels(const ProfileConfiguration&, const std::vector<std::wstring>*) {
    return {};
}

std::wstring EntryLabel(ProfileEntry) { return {}; }
std::wstring BackendLabel(TranslationBackend) { return {}; }
std::wstring RouteLabel(Route) { return {}; }
std::wstring SummaryLine(const EntryStatus&) { return {}; }
std::wstring LanguageLine(const EntryStatus&) { return {}; }

Ticket ResultGuard::begin(uint64_t connectionEpoch) { return {0, connectionEpoch}; }
bool ResultGuard::accepts(const Ticket&, uint64_t) const { return true; }
void ResultGuard::invalidate() {}

} // namespace profilestatus
