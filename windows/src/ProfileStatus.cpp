#include "ProfileStatus.h"
#include "EndpointPolicy.h"
#include "Util.h"
#include <algorithm>

namespace profilestatus {

namespace {

constexpr ProfileEntry kEntries[] = {ProfileEntry::Shortcut1, ProfileEntry::Shortcut2, ProfileEntry::Selection};

bool IsLoopbackHttp(const std::string& url) {
    return execution::ClassifyEndpoint(util::Widen(url)).kind == execution::EndpointKind::LoopbackHttp;
}

} // namespace

Route RouteOf(const TranslationProfile& profile, const execution::ConnectionSnapshot& connection) {
    switch (profile.backend) {
    case TranslationBackend::BuiltIn:
        return Route::Local;
    case TranslationBackend::Ollama:
        return IsLoopbackHttp(connection.ollamaHost) ? Route::Local : Route::Network;
    case TranslationBackend::OpenAI:
        return IsLoopbackHttp(connection.openAIBaseURL) ? Route::Local : Route::Cloud;
    }
    return Route::Cloud;
}

bool IsInstalled(const std::vector<std::wstring>& installed, const std::string& model) {
    if (model.empty()) return false;
    const std::wstring name = util::Widen(model);
    const bool tagless = name.find(L':') == std::wstring::npos;
    return std::any_of(installed.begin(), installed.end(), [&](const std::wstring& candidate) {
        return candidate == name || (tagless && candidate == name + L":latest");
    });
}

std::vector<EntryStatus> Describe(const ProfileConfiguration& configuration,
                                  const execution::ConnectionSnapshot& connection,
                                  const std::vector<std::wstring>* installed) {
    std::vector<EntryStatus> statuses;
    for (ProfileEntry entry : kEntries) {
        EntryStatus status;
        status.entry = entry;
        if (const TranslationProfile* profile = profiles::BoundProfile(configuration, entry)) {
            status.bound = true;
            status.profile = *profile;
            status.route = RouteOf(*profile, connection);
            if (profile->backend == TranslationBackend::Ollama)
                status.model = !installed ? ModelState::Unknown
                             : IsInstalled(*installed, profile->model) ? ModelState::Installed
                                                                       : ModelState::Missing;
        }
        statuses.push_back(std::move(status));
    }
    return statuses;
}

std::vector<Probe> BoundProbes(const ProfileConfiguration& configuration) {
    std::vector<Probe> probes;
    for (ProfileEntry entry : kEntries) {
        const TranslationProfile* profile = profiles::BoundProfile(configuration, entry);
        if (!profile) continue;
        const Probe probe{profile->backend, profile->model};
        if (std::find(probes.begin(), probes.end(), probe) == probes.end()) probes.push_back(probe);
    }
    return probes;
}

std::vector<std::string> PreloadModels(const ProfileConfiguration& configuration,
                                       const std::vector<std::wstring>* installed) {
    std::vector<std::string> models;
    for (const Probe& probe : BoundProbes(configuration)) {
        if (probe.backend != TranslationBackend::Ollama || probe.model.empty()) continue;
        if (installed && !IsInstalled(*installed, probe.model)) continue;
        models.push_back(probe.model);
    }
    return models;
}

std::wstring EntryLabel(ProfileEntry entry) {
    switch (entry) {
    case ProfileEntry::Shortcut1: return L"Shortcut 1";
    case ProfileEntry::Shortcut2: return L"Shortcut 2";
    case ProfileEntry::Selection: return L"Selection";
    }
    return {};
}

std::wstring BackendLabel(TranslationBackend backend) {
    switch (backend) {
    case TranslationBackend::BuiltIn: return L"Built-in";
    case TranslationBackend::Ollama: return L"Ollama";
    case TranslationBackend::OpenAI: return L"OpenAI-compatible API";
    }
    return {};
}

std::wstring RouteLabel(Route route) {
    switch (route) {
    case Route::Local: return L"local";
    case Route::Network: return L"network: selected text goes to another computer";
    case Route::Cloud: return L"cloud: selected text leaves this PC";
    }
    return {};
}

std::wstring SummaryLine(const EntryStatus& status) {
    const std::wstring label = EntryLabel(status.entry) + L": ";
    if (!status.bound) return label + L"No profile is bound";
    std::wstring line = label + util::Widen(status.profile.name) + L" · " + BackendLabel(status.profile.backend);
    if (!status.profile.model.empty()) line += L" " + util::Widen(status.profile.model);
    line += L" · " + RouteLabel(status.route);
    if (status.model == ModelState::Missing) line += L" · model not installed";
    return line;
}

std::wstring LanguageLine(const EntryStatus& status) {
    const std::wstring label = EntryLabel(status.entry) + L": ";
    if (!status.bound) return label + L"No profile is bound";
    const TranslationProfile& profile = status.profile;
    const std::wstring native = lang::ShortName(profile.nativeLanguage);
    const std::wstring foreign = lang::ShortName(profile.foreignLanguage);
    std::wstring pair;
    switch (profile.direction) {
    case TranslationDirection::Auto: pair = native + L" ⇄ " + foreign; break;
    case TranslationDirection::NativeToForeign: pair = native + L" → " + foreign; break;
    case TranslationDirection::ForeignToNative: pair = foreign + L" → " + native; break;
    }
    const std::wstring style = profile.promptMode == PromptMode::Custom
                                   ? std::wstring(L"custom prompt")
                                   : std::wstring(lang::StyleDisplayName(profile.presetStyle));
    return label + util::Widen(profile.name) + L" · " + pair + L" · " + style;
}

Ticket ResultGuard::begin(uint64_t connectionEpoch) {
    open_ = true;
    return {++latest_, connectionEpoch};
}

bool ResultGuard::accepts(const Ticket& ticket, uint64_t currentEpoch) const {
    return open_ && ticket.token != 0 && ticket.token == latest_ && ticket.epoch == currentEpoch;
}

void ResultGuard::invalidate() {
    open_ = false;
}

} // namespace profilestatus
