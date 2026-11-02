#include "UpdateChecker.h"
#include "EndpointPolicy.h"
#include "Http.h"
#include "Json.h"
#include "ProfileBinding.h"
#include "Settings.h"
#include "Util.h"
#include <shellapi.h>
#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

namespace updatechecker {

namespace {

constexpr wchar_t kLatestReleaseApi[] = L"https://api.github.com/repos/kanoha222/typetide/releases/latest";
constexpr wchar_t kReleasesPage[] = L"https://github.com/kanoha222/typetide/releases";

std::mutex g_mu;
std::wstring g_latestUrl;

/// "v1.2.3" 之类的 tag 拆成数字段
std::vector<int> parseVersion(std::string v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    std::vector<int> parts;
    std::string cur;
    for (char c : v + ".") {
        if (c == '.') {
            parts.push_back(cur.empty() ? 0 : atoi(cur.c_str()));
            cur.clear();
        } else if (isdigit((unsigned char)c)) {
            cur += c;
        }
    }
    return parts;
}

bool isNewer(const std::string& latest, const std::string& current) {
    auto l = parseVersion(latest), c = parseVersion(current);
    size_t n = std::max(l.size(), c.size());
    for (size_t i = 0; i < n; ++i) {
        int lp = i < l.size() ? l[i] : 0;
        int cp = i < c.size() ? c[i] : 0;
        if (lp != cp) return lp > cp;
    }
    return false;
}

/// One check of the latest release; a newer one becomes the page ReleasesURL() opens.
CheckResult checkOnce() {
    http::Result r = http::Get(LatestReleaseApi(), L"Accept: application/vnd.github.v3+json\r\n");
    bool ok = false;
    json::Value v = r.ok ? json::Parse(r.body, &ok) : json::Value();
    const std::string tag = ok && !v["prerelease"].asBool() ? v["tag_name"].asString() : std::string();
    if (tag.empty()) return CheckResult::Failed;
    if (!isNewer(tag, TYPETIDE_VERSION_STRING)) return CheckResult::UpToDate;
    std::lock_guard lock(g_mu);
    g_latestUrl = util::Widen(v["html_url"].asString());
    return CheckResult::Newer;
}

/// wParam of the notification: 1 newer, 0 up to date, 2 failed. A background check only
/// reports a newer release; an interactive one reports every outcome.
CheckResult checkWorker(HWND notifyWindow, UINT notifyMessage, bool alwaysNotify) {
    const CheckResult result = checkOnce();
    if (notifyWindow && (result == CheckResult::Newer || alwaysNotify)) PostMessageW(notifyWindow, notifyMessage, static_cast<WPARAM>(result), 0);
    return result;
}

} // namespace

void CheckNow(HWND notifyWindow, UINT notifyMessage) {
    std::thread(checkWorker, notifyWindow, notifyMessage, false).detach();
}

void CheckNowInteractive(HWND notifyWindow, UINT notifyMessage) {
    std::thread(checkWorker, notifyWindow, notifyMessage, true).detach();
}

void CheckIfDue(HWND notifyWindow, UINT notifyMessage) {
    Settings& s = Settings::shared();
    long long now = (long long)time(nullptr);
    if (now - s.lastUpdateCheck < 24 * 3600) return;
    // Saved on its own: never together with any other unsaved change.
    profilebinding::CommitChange(s, [now](SettingsData& d) { d.lastUpdateCheck = now; }, nullptr);
    CheckNow(notifyWindow, notifyMessage);
}

std::wstring ReleasesURL() {
    std::lock_guard lock(g_mu);
    return g_latestUrl.empty() ? std::wstring(kReleasesPage) : g_latestUrl;
}

std::wstring LatestReleaseApi() {
    wchar_t feed[512]{};
    const DWORD length = GetEnvironmentVariableW(L"TYPETIDE_UPDATE_FEED", feed, 512);
    const bool loopback = length > 0 && length < 512 &&
                          execution::ClassifyEndpoint(feed).kind == execution::EndpointKind::LoopbackHttp;
    return loopback ? std::wstring(feed, length) : std::wstring(kLatestReleaseApi);
}

CheckResult CheckForTesting() {
    return checkWorker(nullptr, 0, false);
}

void OpenReleasesPage() {
    ShellExecuteW(nullptr, L"open", ReleasesURL().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace updatechecker
