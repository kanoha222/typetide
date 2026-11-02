//
//  TranslationSession.h — pure bookkeeping for user translation sessions: which stream
//  callbacks are still current, what closing a popup cancels, and whether a pending paste
//  may still commit input. Ledger is main-thread only; PasteGuard::allows may run on any
//  thread (the replacer checks it right before Ctrl+A / Ctrl+V).
//
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace session {

enum class CloseReason {
    Dismiss,        // Esc / click outside / close button: cancels that session
    ReplaceCommit,  // Replace clicked: closes the popup, keeps that session's paste alive
    Superseded,     // a newer popup or error popup replaced it: invalidates only that session
    AppExit,        // quitting: cancels the current session
};

/// Captured by stream callbacks when they are created: never a request id still awaiting
/// return, never mutable "current" state.
struct Ticket {
    uint64_t session = 0;
    uint64_t attempt = 0;
};

/// Final input-commit check for one session.
struct PasteGuard {
    std::shared_ptr<const std::atomic<uint64_t>> live;
    uint64_t session = 0;
    std::uintptr_t target = 0;  // foreground window when the text was captured (0 = unknown)
    std::uintptr_t focus = 0;   // its focused control then (0 = not known, only the window is checked)
    /// The session is still the latest one and was not cancelled.
    bool isCurrent() const;
    /// True while the session may still commit input into `foreground` whose focused control is
    /// `focused`. An unknown target never allows input.
    bool allows(std::uintptr_t foreground, std::uintptr_t focused = 0) const;
};

class Ledger {
public:
    Ledger();

    struct Started {
        uint64_t session = 0;
        std::vector<uint64_t> cancel;  // requests of the superseded session
    };
    /// A new user translation. Every earlier session goes stale: its active request is
    /// returned for cancellation and its pending paste can no longer commit.
    Started beginSession();

    struct Attempt {
        Ticket ticket;
        std::vector<uint64_t> cancel;  // the previous attempt's request
    };
    /// Before each Stream() of `session` (first run, retry, retarget). A stale or closed
    /// session gets a ticket that is never accepted.
    Attempt beginAttempt(uint64_t session);

    /// After Stream() returns, so a later close can cancel that request.
    void attachRequest(const Ticket& ticket, uint64_t requestId);

    /// A delta / done / result carrying `ticket` should still be applied.
    bool accepts(const Ticket& ticket) const;

    /// The popup bound to `session` closed. Returns the requests to cancel. A stale session
    /// never affects a newer one; AppExit always cancels the current session.
    std::vector<uint64_t> close(uint64_t session, CloseReason reason);

    PasteGuard pasteGuard(uint64_t session, std::uintptr_t target, std::uintptr_t focus = 0) const;

    uint64_t current() const { return current_; }

private:
    std::shared_ptr<std::atomic<uint64_t>> live_;
    uint64_t current_ = 0;
    uint64_t attempt_ = 0;
    uint64_t request_ = 0;
    bool closed_ = false;
};

} // namespace session
