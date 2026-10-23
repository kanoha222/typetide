#include "HttpUrl.h"

namespace http {

// RED scaffold: compile shell only. The WinHttpCrackUrl-backed implementation
// (borrowed-pointer components copied into values, no Open/Connect/Request)
// is added only after the behavioral RED has been observed on CI.
UrlParts CrackUrl(const std::wstring& /*url*/) {
    return {};
}

} // namespace http
