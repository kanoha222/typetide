#include "TranslationProfile.h"
#include <windows.h>
#include <objbase.h>

std::string profiles::NewId() {
    GUID identifier{};
    if (FAILED(CoCreateGuid(&identifier))) return {};
    wchar_t text[39]{};
    if (StringFromGUID2(identifier, text, 39) != 39) return {};
    std::string result;
    result.reserve(36);
    for (size_t index = 1; index <= 36; ++index) {
        const wchar_t character = text[index];
        result.push_back(static_cast<char>(character >= L'A' && character <= L'F'
                                              ? character + (L'a' - L'A') : character));
    }
    return result;
}
