#include "TranslationExecutionSupport.h"
#include <string>
#include <vector>

// Authority matrix for execution::ClassifyEndpoint. Pure, offline, synthetic hosts.
// A "NotLoopback" row must be a READY classification (Remote or Invalid): a NotReady
// scaffold result never satisfies a negative case.
namespace execution_test {
namespace {

using execution::ClassifyEndpoint;
using execution::EndpointInfo;
using execution::EndpointKind;

enum class Want { Loopback, Https, Remote, Invalid, NotLoopback };

struct Row {
    std::wstring url;
    Want want;
};

bool Matches(EndpointKind kind, Want want) {
    switch (want) {
    case Want::Loopback: return kind == EndpointKind::LoopbackHttp;
    case Want::Https: return kind == EndpointKind::Https;
    case Want::Remote: return kind == EndpointKind::RemoteHttp;
    case Want::Invalid: return kind == EndpointKind::Invalid;
    case Want::NotLoopback:
        return kind == EndpointKind::RemoteHttp || kind == EndpointKind::Invalid;
    }
    return false;
}

void RunRows(const std::vector<Row>& rows, const char* name) {
    for (const Row& row : rows) {
        const EndpointInfo info = ClassifyEndpoint(row.url);
        Check(Matches(info.kind, row.want), name);
        if (row.want != Want::Invalid && row.want != Want::NotLoopback &&
            info.kind != EndpointKind::NotReady)
            Check(info.url == row.url, "classifier returns the exact input string unchanged");
    }
}

void TestLoopbackAndHttps() {
    RunRows({{L"http://localhost", Want::Loopback},
             {L"http://localhost/", Want::Loopback},
             {L"http://localhost:11434", Want::Loopback},
             {L"http://LocalHost:8080/x", Want::Loopback},
             {L"http://127.0.0.1", Want::Loopback},
             {L"http://127.0.0.1:1", Want::Loopback},
             {L"http://127.0.0.1:65535/v1", Want::Loopback},
             {L"http://[::1]", Want::Loopback},
             {L"http://[::1]:11434/v1", Want::Loopback},
             {L"http://localhost:1234/v1%20x", Want::Loopback}},
            "loopback HTTP authority accepted");
    RunRows({{L"https://api.example.test", Want::Https},
             {L"https://api.example.test/v1", Want::Https},
             {L"https://api.example.test:8443/v1", Want::Https},
             {L"https://localhost", Want::Https},
             {L"https://127.0.0.1:8443", Want::Https},
             {L"https://[::1]:8443", Want::Https},
             {L"https://api.example.test/v1%2Fx", Want::Https}},
            "HTTPS authority accepted with kind Https (percent allowed in the path)");
}

void TestRemoteAndLookalikeHosts() {
    RunRows({{L"http://api.example.test", Want::Remote},
             {L"http://api.example.test:11434", Want::Remote},
             {L"http://192.168.1.5:11434", Want::Remote},
             {L"http://localhost.evil.test", Want::Remote},
             {L"http://xlocalhost", Want::Remote}},
            "remote HTTP host is classified RemoteHttp");
    RunRows({{L"http://127.0.0.2", Want::NotLoopback},
             {L"http://127.1", Want::NotLoopback},
             {L"http://2130706433", Want::NotLoopback},
             {L"http://0x7f.0.0.1", Want::NotLoopback},
             {L"http://127.000.000.001", Want::NotLoopback},
             {L"http://0.0.0.0", Want::NotLoopback},
             {L"http://localhost.", Want::NotLoopback},
             {L"http://127.0.0.1.", Want::NotLoopback},
             {L"http://[0:0:0:0:0:0:0:1]", Want::NotLoopback},
             {L"http://[::ffff:127.0.0.1]", Want::NotLoopback},
             {L"http://[::0001]", Want::NotLoopback}},
            "only the three exact tokens are loopback");
}

std::vector<Row> InvalidAuthorityRows() {
    const std::wstring nulHost(L"http://local\0host", 17);
    return {{L"http://user@localhost", Want::Invalid},
            {L"http://localhost@evil.test", Want::Invalid},
            {L"http://user:pw@localhost", Want::Invalid},
            {L"http://:@localhost", Want::Invalid},
            {L"http://local%68ost", Want::Invalid},
            {L"http://localhost%2f", Want::Invalid},
            {L"http://evil.test\\@localhost", Want::Invalid},
            {L"http://localhost\\", Want::Invalid},
            {L"http://local host", Want::Invalid},
            {L"http://localhost\t", Want::Invalid},
            {L"http://localhost\n", Want::Invalid},
            {L"http://local\x01host", Want::Invalid},
            {L"http://local\x7Fhost", Want::Invalid},
            {nulHost, Want::Invalid},
            {L" http://localhost", Want::Invalid},
            {L"http://localhost ", Want::Invalid},
            {L"http://l\u043Ecalhost", Want::Invalid},
            {L"http://\uFF4Cocalhost", Want::Invalid}};
}

std::vector<Row> InvalidShapeRows() {
    return {{L"http://localhost:0", Want::Invalid},
            {L"http://localhost:65536", Want::Invalid},
            {L"http://localhost:99999", Want::Invalid},
            {L"http://localhost:-1", Want::Invalid},
            {L"http://localhost:+80", Want::Invalid},
            {L"http://localhost:abc", Want::Invalid},
            {L"http://localhost:80a", Want::Invalid},
            {L"http://localhost:", Want::Invalid},
            {L"http://localhost:11434:80", Want::Invalid},
            {L"http://[::1]:99999", Want::Invalid},
            {L"http://[::1", Want::Invalid},
            {L"http://[::1]x", Want::Invalid},
            {L"http://", Want::Invalid},
            {L"http:///path", Want::Invalid},
            {L"", Want::Invalid},
            {L"localhost:11434", Want::Invalid},
            {L"ftp://localhost", Want::Invalid},
            {L"ws://localhost", Want::Invalid},
            {L"file:///c:/x", Want::Invalid}};
}

void TestInvalidInputs() {
    RunRows(InvalidAuthorityRows(), "unsafe authority is Invalid");
    RunRows(InvalidShapeRows(), "malformed port, host or scheme is Invalid");
}

void TestParsedFields() {
    EndpointInfo info = ClassifyEndpoint(L"http://localhost");
    Check(info.kind == EndpointKind::LoopbackHttp && info.port == 80 && !info.explicitPort,
          "default HTTP port is 80 and not explicit");
    info = ClassifyEndpoint(L"https://api.example.test/v1");
    Check(info.kind == EndpointKind::Https && info.port == 443 && info.hasPath && !info.hasQueryOrFragment,
          "default HTTPS port is 443; path detected");
    info = ClassifyEndpoint(L"http://[::1]:11434/v1");
    Check(info.host == L"[::1]" && info.port == 11434 && info.explicitPort,
          "IPv6 host keeps its brackets; explicit port parsed");
    info = ClassifyEndpoint(L"https://api.example.test/v1?x=1");
    Check(info.kind == EndpointKind::Https && info.hasQueryOrFragment,
          "a query does not change the authority kind but is flagged for the builder");
    info = ClassifyEndpoint(L"http://localhost:11434#f");
    Check(info.kind == EndpointKind::LoopbackHttp && info.hasQueryOrFragment,
          "a fragment is flagged and the authority is still loopback");
}

} // namespace

void RunClassifierTests() {
    TestLoopbackAndHttps();
    TestRemoteAndLookalikeHosts();
    TestInvalidInputs();
    TestParsedFields();
}

} // namespace execution_test
