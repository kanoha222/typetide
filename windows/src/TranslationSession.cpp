#include "TranslationSession.h"

namespace session {

namespace {

std::vector<uint64_t> Take(uint64_t* request) {
    std::vector<uint64_t> cancel;
    if (*request) cancel.push_back(*request);
    *request = 0;
    return cancel;
}

} // namespace

bool PasteGuard::isCurrent() const {
    return live && session != 0 && live->load() == session;
}

bool PasteGuard::allows(std::uintptr_t foreground, std::uintptr_t focused) const {
    if (!isCurrent() || target == 0 || target != foreground) return false;
    return focus == 0 || focus == focused;
}

Ledger::Ledger() : live_(std::make_shared<std::atomic<uint64_t>>(0)) {}

Ledger::Started Ledger::beginSession() {
    Started started;
    started.cancel = Take(&request_);
    started.session = ++current_;
    attempt_ = 0;
    closed_ = false;
    live_->store(current_);
    return started;
}

Ledger::Attempt Ledger::beginAttempt(uint64_t session) {
    Attempt attempt;
    attempt.ticket.session = session;
    if (session == 0 || session != current_ || closed_) return attempt;  // attempt 0: never accepted
    attempt.cancel = Take(&request_);
    attempt.ticket.attempt = ++attempt_;
    return attempt;
}

void Ledger::attachRequest(const Ticket& ticket, uint64_t requestId) {
    if (ticket.attempt != 0 && ticket.session == current_ && ticket.attempt == attempt_) request_ = requestId;
}

bool Ledger::accepts(const Ticket& ticket) const {
    return ticket.attempt != 0 && ticket.session == current_ && ticket.attempt == attempt_ && !closed_;
}

std::vector<uint64_t> Ledger::close(uint64_t session, CloseReason reason) {
    if (reason != CloseReason::AppExit && (session == 0 || session != current_)) return {};
    closed_ = true;
    if (reason == CloseReason::ReplaceCommit) return {};  // the paste stays allowed
    live_->store(0);
    return Take(&request_);
}

PasteGuard Ledger::pasteGuard(uint64_t session, std::uintptr_t target, std::uintptr_t focus) const {
    return {live_, session, target, focus};
}

} // namespace session
