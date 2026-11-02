#include "Settings.h"
#include "Credentials.h"
#include "SettingsCodec.h"
#include "SettingsDiagnostics.h"
#include "SettingsStorage.h"
#include "Util.h"
#include <limits>
#include <type_traits>
#include <utility>

namespace {
constexpr char kCleanupWarning[] = "Settings committed; obsolete credential cleanup is incomplete";

struct TransactionPlan {
    SettingsData data;
    std::string ref;
    std::string bytes;
    uint64_t epoch = 0;
    bool stage = false;
};

bool Prepare(const SettingsData& candidate, const SettingsData& committed, const std::string& knownRef,
             uint64_t epoch, TransactionPlan& plan, std::string& error) {
    if (!settings_codec::Validate(candidate, error)) return false;
    const bool changed = settings_codec::ConnectionChanged(committed, candidate);
    if (changed && epoch == std::numeric_limits<uint64_t>::max()) {
        error = "Connection version exhausted; restart required";
        return false;
    }
    plan.data = candidate;
    plan.epoch = epoch + (changed ? 1 : 0);
    if (!candidate.openAIKey.empty()) {
        const bool reusable = candidate.openAIKey == committed.openAIKey && credentials::IsValidRef(knownRef);
        plan.ref = reusable ? knownRef : profiles::NewId();
        if (!credentials::IsValidRef(plan.ref)) {
            error = "Cannot create a unique credential reference";
            return false;
        }
        plan.stage = !reusable; // Legacy key with no ref always receives a new versioned record.
    }
    plan.bytes = json::Value(settings_codec::Encode(plan.data, plan.ref)).dump();
    if (plan.bytes.empty() || plan.bytes.size() > profiles::MaxSettingsBytes) {
        error = "Complete settings exceed the 1 MiB byte limit";
        return false;
    }
    return true;
}

bool SafeDelete(const std::string& ref, bool legacy = false) noexcept {
    try {
        return legacy ? credentials::DeleteLegacyFixedKey() : credentials::DeleteKey(ref);
    } catch (...) {
        settings_diagnostics::Log("settings: credential cleanup raised an operational failure");
        return false;
    }
}

class StagedCredential {
public:
    explicit StagedCredential(const std::string& ref) : ref_(ref) {}
    ~StagedCredential() { Rollback(); }
    bool Stage(const std::string& key) {
        owned_ = credentials::StageKey(ref_, key);
        return owned_;
    }
    void Release() { owned_ = false; }
    bool Rollback() noexcept {
        if (!std::exchange(owned_, false)) return true;
        const bool removed = SafeDelete(ref_);
        if (!removed) settings_diagnostics::Log("settings: staged credential rollback cleanup incomplete");
        return removed;
    }
private:
    const std::string& ref_;
    bool owned_ = false;
};

bool CleanupPredecessor(const std::string& oldRef, const std::string& newRef, bool legacyOrigin) noexcept {
    bool clean = true;
    if (!oldRef.empty() && oldRef != newRef) clean = SafeDelete(oldRef);
    if (legacyOrigin) clean = SafeDelete({}, true) && clean;
    return clean;
}

} // namespace

bool Settings::commit(const Settings& candidate, std::string* error) {
    if (isReadOnly()) {
        if (error) *error = storageError_.empty() ? "Settings storage is read-only" : storageError_;
        return false;
    }
    try {
        return commitReady(candidate, error);
    } catch (...) {
        if (error) *error = "Cannot prepare or persist settings; predecessor retained";
        settings_diagnostics::Log("settings: transaction failed before publication");
        return false;
    }
}

bool Settings::commitReady(const Settings& candidate, std::string* error) {
    TransactionPlan plan;
    std::string failure;
    if (!Prepare(candidate, committed_, credentialRef_, connectionEpoch_, plan, failure)) {
        if (error) *error = failure;
        return false;
    }
    const auto oldRef = credentialRef_;
    const bool oldLegacyOrigin = legacyOrigin_;
    Settings prepared = *this;
    prepared.adopt(plan.data, plan.ref, false, true, plan.epoch);
    prepared.storageWarning_ = kCleanupWarning; // Allocate before staging/file replacement.
    StagedCredential staged(plan.ref);
    if (plan.stage && !staged.Stage(plan.data.openAIKey)) {
        if (error) *error = "Cannot stage API credential; predecessor retained";
        return false;
    }
    if (!settings_storage::Replace(filePath(), plan.bytes, failure)) {
        if (!staged.Rollback()) failure = "Settings not committed; staged credential cleanup incomplete";
        if (error) *error = failure;
        return false;
    }
    staged.Release();
    static_assert(std::is_nothrow_move_assignable_v<Settings>);
    *this = std::move(prepared); // One complete publication after the durable file replacement.
    if (CleanupPredecessor(oldRef, credentialRef_, oldLegacyOrigin)) storageWarning_.clear();
    else settings_diagnostics::Log("settings: committed successfully; obsolete credential cleanup incomplete");
    if (error) error->clear();
    return true;
}
