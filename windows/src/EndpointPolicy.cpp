#include "EndpointPolicy.h"

namespace execution {

// RED scaffold: compile shell only (kind stays NotReady, url stays empty).
EndpointInfo ClassifyEndpoint(const std::wstring& /*url*/) {
    return {};
}

} // namespace execution
