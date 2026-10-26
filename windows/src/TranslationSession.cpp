#include "TranslationSession.h"

namespace session {

// RED scaffold: the ledger reports nothing until the lifecycle tests have been observed
// failing on CI.

bool PasteGuard::allows(std::uintptr_t /*foreground*/) const {
    return false;
}

Ledger::Ledger() : live_(std::make_shared<std::atomic<uint64_t>>(0)) {}

Ledger::Started Ledger::beginSession() {
    return {};
}

Ledger::Attempt Ledger::beginAttempt(uint64_t /*session*/) {
    return {};
}

void Ledger::attachRequest(const Ticket& /*ticket*/, uint64_t /*requestId*/) {}

bool Ledger::accepts(const Ticket& /*ticket*/) const {
    return false;
}

std::vector<uint64_t> Ledger::close(uint64_t /*session*/, CloseReason /*reason*/) {
    return {};
}

PasteGuard Ledger::pasteGuard(uint64_t /*session*/, std::uintptr_t /*target*/) const {
    return {};
}

} // namespace session
