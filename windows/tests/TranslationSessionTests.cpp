#include "../src/TranslationSession.h"
#include "TranslationExecutionSupport.h"
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

// Session lifecycle as ordered event sequences: the "barriers" are the exact points at which
// a callback, close or paste check happens relative to newer user actions.
namespace execution_test {
namespace {

using session::CloseReason;
using session::Ledger;
using session::Ticket;

constexpr std::uintptr_t kEditor = 0x1010;
constexpr std::uintptr_t kOtherWindow = 0x2020;

bool Same(const std::vector<uint64_t>& actual, const std::vector<uint64_t>& expected) {
    return actual == expected;
}

Ticket Run(Ledger& ledger, uint64_t session, uint64_t requestId, std::vector<uint64_t>* cancelled = nullptr) {
    const Ledger::Attempt attempt = ledger.beginAttempt(session);
    if (cancelled) *cancelled = attempt.cancel;
    ledger.attachRequest(attempt.ticket, requestId);
    return attempt.ticket;
}

void TestFreshLedger() {
    Ledger ledger;
    Check(ledger.current() == 0 && !ledger.accepts(Ticket{}), "a fresh ledger accepts nothing");
    Check(!ledger.pasteGuard(0, kEditor).allows(kEditor), "a fresh ledger allows no paste");
    Check(!session::PasteGuard{}.allows(kEditor), "an empty guard allows no paste");
}

void TestNewSessionSupersedes() {
    Ledger ledger;
    const uint64_t first = ledger.beginSession().session;
    const Ticket a = Run(ledger, first, 11);
    Check(first != 0 && ledger.accepts(a), "the running attempt of the current session is accepted");
    const session::PasteGuard oldGuard = ledger.pasteGuard(first, kEditor);
    const Ledger::Started next = ledger.beginSession();
    Check(next.session != first && Same(next.cancel, {11}), "a new session cancels the previous request");
    Check(!ledger.accepts(a), "callbacks of a superseded session are dropped");
    Check(!oldGuard.allows(kEditor), "a guard taken earlier sees the newer session (delayed input commit)");
    Check(ledger.pasteGuard(next.session, kEditor).allows(kEditor), "the new session may paste");
    Check(Same(ledger.beginSession().cancel, {}), "a session without a request cancels nothing");
}

void TestCallbackBeforeAttach() {
    Ledger ledger;
    const uint64_t current = ledger.beginSession().session;
    const Ledger::Attempt attempt = ledger.beginAttempt(current);
    Check(ledger.accepts(attempt.ticket),
          "a synchronous cache hit is accepted before Stream() has returned its id");
    ledger.attachRequest(attempt.ticket, 31);
    Check(ledger.accepts(attempt.ticket), "attaching the id keeps the ticket current");
}

void TestRetryAndRetarget() {
    Ledger ledger;
    const uint64_t current = ledger.beginSession().session;
    const Ticket first = Run(ledger, current, 11);
    std::vector<uint64_t> cancelled;
    const Ticket second = Run(ledger, current, 12, &cancelled);
    Check(Same(cancelled, {11}), "a retarget cancels the previous attempt's request");
    Check(!ledger.accepts(first) && ledger.accepts(second), "only the latest attempt is accepted");
    Check(second.session == current, "retry and retarget stay in the same session");
}

void TestStaleCloseIsolated() {
    Ledger ledger;
    const uint64_t first = ledger.beginSession().session;
    Run(ledger, first, 11);
    const uint64_t second = ledger.beginSession().session;
    const Ticket current = Run(ledger, second, 22);
    for (CloseReason reason : {CloseReason::Dismiss, CloseReason::Superseded, CloseReason::ReplaceCommit}) {
        Check(Same(ledger.close(first, reason), {}), "closing a stale popup cancels nothing");
        Check(ledger.accepts(current), "closing a stale popup never touches the newer session");
    }
    Check(ledger.pasteGuard(second, kEditor).allows(kEditor), "the newer session may still paste");
}

void TestDismiss() {
    Ledger ledger;
    const uint64_t current = ledger.beginSession().session;
    const Ticket ticket = Run(ledger, current, 11);
    Check(Same(ledger.close(current, CloseReason::Dismiss), {11}), "dismiss cancels that session's request");
    Check(!ledger.accepts(ticket), "a dismissed session accepts no more callbacks");
    Check(!ledger.pasteGuard(current, kEditor).allows(kEditor), "a dismissed session cannot paste");
    Check(!ledger.accepts(ledger.beginAttempt(current).ticket), "a dismissed session cannot start a retry");
    Check(Same(ledger.close(current, CloseReason::Dismiss), {}), "a second close cancels nothing more");
}

void TestSupersededPopup() {
    Ledger ledger;
    const uint64_t current = ledger.beginSession().session;
    Run(ledger, current, 11);
    Check(Same(ledger.close(current, CloseReason::Superseded), {11}),
          "an error popup replacing the session popup cancels its request");
    Check(Same(ledger.beginSession().cancel, {}), "the cancelled request is not cancelled twice");
}

void TestReplaceCommit() {
    Ledger ledger;
    const uint64_t current = ledger.beginSession().session;
    const Ticket ticket = Run(ledger, current, 11);
    const session::PasteGuard guard = ledger.pasteGuard(current, kEditor);
    Check(Same(ledger.close(current, CloseReason::ReplaceCommit), {}), "replace commit cancels nothing");
    Check(!ledger.accepts(ticket), "the committed popup takes no more stream callbacks");
    Check(guard.allows(kEditor), "the committed paste may still go into the captured window");
    Check(!guard.allows(kOtherWindow), "the paste is refused when another window is in front");
    ledger.beginSession();
    Check(!guard.allows(kEditor), "a newer translation cancels the not-yet-committed paste");
}

void TestUnknownTarget() {
    Ledger ledger;
    const uint64_t current = ledger.beginSession().session;
    const session::PasteGuard guard = ledger.pasteGuard(current, 0);
    Check(guard.allows(kEditor) && guard.allows(kOtherWindow),
          "without a captured window only the session is checked");
}

void TestAppExit() {
    Ledger ledger;
    const uint64_t first = ledger.beginSession().session;
    const uint64_t current = ledger.beginSession().session;
    Run(ledger, current, 22);
    const session::PasteGuard guard = ledger.pasteGuard(current, kEditor);
    Check(Same(ledger.close(first, CloseReason::AppExit), {22}), "app exit cancels the current session");
    Check(!guard.allows(kEditor), "app exit cancels a pending paste");
}

void TestWithBinding() {
    ProfileConfiguration configuration = profiles::Defaults();
    TranslationProfile extra = MakeProfile(TranslationBackend::Ollama);
    extra.id = "extra-profile";
    configuration.profiles.push_back(extra);
    const ProfileConfiguration before = configuration;
    const ProfileConfiguration bound =
        profiles::WithBinding(configuration, ProfileEntry::Shortcut2, "extra-profile");
    Check(bound.bindings.shortcut2ProfileId == "extra-profile", "the chosen entry is rebound");
    Check(bound.bindings.shortcut1ProfileId == before.bindings.shortcut1ProfileId &&
              bound.bindings.selectionProfileId == before.bindings.selectionProfileId &&
              bound.profiles == before.profiles,
          "other entries and the profiles are untouched");
    Check(configuration == before, "the input configuration is not modified");
    Check(profiles::Validate(bound).ok, "a binding to an existing profile validates");
    const ProfileConfiguration selection =
        profiles::WithBinding(configuration, ProfileEntry::Selection, "extra-profile");
    Check(selection.bindings.selectionProfileId == "extra-profile", "the selection entry can be rebound");
    Check(!profiles::Validate(profiles::WithBinding(configuration, ProfileEntry::Shortcut1, "missing")).ok,
          "a binding to an unknown profile does not validate");
}

} // namespace

void RunSessionTests() {
    TestFreshLedger();
    TestNewSessionSupersedes();
    TestCallbackBeforeAttach();
    TestRetryAndRetarget();
    TestStaleCloseIsolated();
    TestDismiss();
    TestSupersededPopup();
    TestReplaceCommit();
    TestUnknownTarget();
    TestAppExit();
    TestWithBinding();
}

} // namespace execution_test
