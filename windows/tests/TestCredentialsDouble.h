#pragma once
#include <cstddef>

namespace test_credentials {

size_t UnexpectedLoadCalls();
size_t UnexpectedSaveCalls();
void ResetCounters();

} // namespace test_credentials
