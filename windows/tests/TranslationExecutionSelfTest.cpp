#include "TranslationExecutionSupport.h"
#include <cstdio>

// Standalone pure-logic entry point. The exit code is 0 only when every check
// passed; any failed check yields exit 1 (never an infrastructure code).
int main() {
    using namespace execution_test;
    RunPresetTests();
    RunRenderTests();
    RunDirectionTests();
    RunRequestTests();
    RunBuiltInTests();
    RunClassifierTests();
    RunPrepareEndpointTests();
    RunCacheTests();
    std::printf("[execution] %d failure(s)\n", FailureCount());
    return FailureCount() == 0 ? 0 : 1;
}
