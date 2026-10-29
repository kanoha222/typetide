//
//  ProfileStatus.h — pure status of the committed bindings for the Settings window: which
//  profile each entry runs, where its text goes (local / network / cloud), whether a bound
//  Ollama model is installed, which models connection tests and preload use, and a version
//  guard that drops stale asynchronous results (model discovery, connection tests).
//
//  No Settings, Win32 UI or network access: callers pass committed values in.
//
#pragma once
#include "TranslationExecution.h"
#include "TranslationProfile.h"
#include <cstdint>
#include <string>
#include <vector>

namespace profilestatus {

/// Where a profile's selected text is processed.
enum class Route {
    Local,               // built-in model, or Ollama / an OpenAI-compatible server on loopback HTTP
    Network,             // Ollama on another computer over HTTPS
    NetworkUnencrypted,  // Ollama on another computer over plain HTTP
    Cloud,               // an OpenAI-compatible endpoint that is not loopback HTTP
};

Route RouteOf(const TranslationProfile& profile, const execution::ConnectionSnapshot& connection);

/// Replace actions must show the popup first (user decision): the reply crosses the network
/// unencrypted, so it could have been altered before it would be pasted into the user's app.
bool RequiresPreview(const TranslationProfile& profile, const execution::ConnectionSnapshot& connection);

/// Ollama tag matching: an exact name, or a name without a tag against its ":latest" install.
bool IsInstalled(const std::vector<std::wstring>& installed, const std::string& model);

enum class ModelState {
    NotApplicable,  // built-in model or a cloud model: nothing to discover
    Unknown,        // Ollama, but the installed list has not arrived (or failed)
    Installed,
    Missing,        // shown to the user; never replaced by another model
};

struct EntryStatus {
    ProfileEntry entry = ProfileEntry::Shortcut1;
    bool bound = false;           // false when the binding does not resolve to a profile
    TranslationProfile profile;   // a copy; meaningful only when bound
    Route route = Route::Local;
    ModelState model = ModelState::NotApplicable;
};

/// Shortcut 1, Shortcut 2 and Selection, in that order. `installed` == nullptr means the
/// Ollama model list is not known.
std::vector<EntryStatus> Describe(const ProfileConfiguration& configuration,
                                  const execution::ConnectionSnapshot& connection,
                                  const std::vector<std::wstring>* installed);

/// One backend + model a connection test checks.
struct Probe {
    TranslationBackend backend = TranslationBackend::BuiltIn;
    std::string model;
    bool operator==(const Probe&) const = default;
};

/// Distinct backend + model pairs of the bound profiles, in entry order.
std::vector<Probe> BoundProbes(const ProfileConfiguration& configuration);

/// Bound Ollama models, each once, in entry order. When `installed` is known, models that
/// are not installed are left out; nothing is ever substituted for them.
std::vector<std::string> PreloadModels(const ProfileConfiguration& configuration,
                                       const std::vector<std::wstring>* installed);

std::wstring EntryLabel(ProfileEntry entry);
std::wstring BackendLabel(TranslationBackend backend);
std::wstring RouteLabel(Route route);
/// "Shortcut 1: Name · Ollama model · local", plus " · model not installed" when missing.
std::wstring SummaryLine(const EntryStatus& status);
/// "Shortcut 1: Name · 中文 ⇄ English · Faithful" (or "custom prompt").
std::wstring LanguageLine(const EntryStatus& status);

/// Identity of one asynchronous request: its sequence number and the connection epoch it
/// started under.
struct Ticket {
    uint64_t token = 0;
    uint64_t epoch = 0;
};

/// Accepts a result only when it is the latest request AND the connection it used is still
/// the committed one. Main thread only.
class ResultGuard {
public:
    Ticket begin(uint64_t connectionEpoch);
    bool accepts(const Ticket& ticket, uint64_t currentEpoch) const;
    /// Every request in flight becomes stale.
    void invalidate();

private:
    uint64_t latest_ = 0;
    bool open_ = false;
};

} // namespace profilestatus
