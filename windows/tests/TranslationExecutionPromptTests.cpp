#include "TranslationExecutionGoldens.h"
#include "TranslationExecutionSupport.h"
#include <cstddef>
#include <initializer_list>
#include <string>

// Preset byte goldens and the custom-template renderer. Expected strings are
// spelled out by hand from the CURRENT Root builders (see Goldens.h), never
// produced by the code under test.
namespace execution_test {
namespace {

using execution::PresetPlainPrompt;
using execution::PresetSystemPrompt;
using execution::RenderCustomPrompt;
using execution::RenderedPrompt;

struct StyleRow {
    RewriteStyle style;
    const char* suffix;
};
const StyleRow kStyles[] = {
    {RewriteStyle::Faithful, ""},
    {RewriteStyle::Formal, GOLD_STYLE_FORMAL},
    {RewriteStyle::Casual, GOLD_STYLE_CASUAL},
    {RewriteStyle::Polished, GOLD_STYLE_POLISHED},
};

// Same order as lang::All.
const char* const kPromptNames[] = {
    "English", "Chinese", "Hindi", "Spanish", "French",
    "Arabic", "Bengali", "Russian", "Portuguese", "Indonesian",
};

void TestSystemGoldens() {
    Check(PresetSystemPrompt(Language::Chinese, Language::English, RewriteStyle::Faithful) ==
              GOLD_SYS_EN_ZH_FAITHFUL,
          "preset system: English->Chinese faithful golden");
    for (const StyleRow& row : kStyles) {
        const std::string known =
            std::string(GOLD_SYS_HEAD "Spanish into Russian.") + row.suffix + GOLD_SYS_TAIL;
        Check(PresetSystemPrompt(Language::Russian, Language::Spanish, row.style) == known,
              "preset system: every style, known source");
        const std::string unknown =
            std::string(GOLD_SYS_HEAD "the detected language into Russian.") + row.suffix +
            GOLD_SYS_TAIL;
        Check(PresetSystemPrompt(Language::Russian, std::nullopt, row.style) == unknown,
              "preset system: unknown source says 'the detected language'");
    }
}

void TestPlainGoldens() {
    Check(PresetPlainPrompt(L"Hello", Language::Chinese, Language::English,
                            RewriteStyle::Faithful) == GOLD_PLAIN_EN_ZH_HELLO,
          "preset plain: English->Chinese faithful golden");
    for (const StyleRow& row : kStyles) {
        const std::string known =
            std::string(GOLD_PLAIN_HEAD "Spanish to Russian.") + row.suffix + GOLD_PLAIN_TAIL + "ok";
        Check(PresetPlainPrompt(L"ok", Language::Russian, Language::Spanish, row.style) == known,
              "preset plain: every style, known source");
        const std::string unknown =
            std::string(GOLD_PLAIN_HEAD "the source language to Russian.") + row.suffix +
            GOLD_PLAIN_TAIL + "ok";
        Check(PresetPlainPrompt(L"ok", Language::Russian, std::nullopt, row.style) == unknown,
              "preset plain: unknown source says 'the source language'");
    }
}

void TestPlainTextVerbatim() {
    const std::string prefix =
        std::string(GOLD_PLAIN_HEAD "English to Chinese.") + GOLD_PLAIN_TAIL;
    Check(PresetPlainPrompt(L"  hi {{target_language}}\n", Language::Chinese, Language::English,
                            RewriteStyle::Faithful) == prefix + "  hi {{target_language}}\n",
          "preset plain: text kept verbatim, whitespace and braces untouched");
    Check(PresetPlainPrompt(L"你好", Language::Chinese, Language::English,
                            RewriteStyle::Faithful) == prefix + "\xE4\xBD\xA0\xE5\xA5\xBD",
          "preset plain: non-ASCII text appended as UTF-8");
}

void TestEveryPromptName() {
    for (std::size_t i = 0; i < sizeof(lang::All) / sizeof(lang::All[0]); ++i) {
        const std::string expected = std::string(GOLD_SYS_HEAD "the detected language into ") +
                                     kPromptNames[i] + "." + GOLD_SYS_TAIL;
        Check(PresetSystemPrompt(lang::All[i], std::nullopt, RewriteStyle::Faithful) == expected,
              "preset system: every language uses its English prompt name");
    }
}

void TestRenderKnownTokens() {
    RenderedPrompt r = RenderCustomPrompt("From {{source_language}} to {{target_language}}.",
                                          Language::English, Language::Chinese);
    Check(r.ok && r.text == "From English to Chinese.", "custom render: both known tokens");
    Check(r.warnings.empty() && r.unknownTokens.empty(), "custom render: known tokens never warn");
    r = RenderCustomPrompt("{{target_language}}/{{target_language}}", Language::English,
                           Language::Russian);
    Check(r.ok && r.text == "Russian/Russian", "custom render: repeated token replaced each time");
    r = RenderCustomPrompt("{{source_language}}>{{target_language}}", Language::Russian,
                           Language::Indonesian);
    Check(r.ok && r.text == "Russian>Indonesian", "custom render: third language keeps its name");
}

void TestRenderUnknownSource() {
    const RenderedPrompt r =
        RenderCustomPrompt("S={{source_language}}", std::nullopt, Language::English);
    Check(r.ok && r.text == "S=auto-detected language",
          "custom render: unknown source is exactly 'auto-detected language' (no 'the')");
}

void TestRenderWhitespaceAndBlank() {
    const std::string raw = "\n  {{target_language}}\t\r\n"
                            "\xE4\xBD\xA0\xE5\xA5\xBD"
                            "  \n\n";
    const std::string want = "\n  Chinese\t\r\n"
                             "\xE4\xBD\xA0\xE5\xA5\xBD"
                             "  \n\n";
    RenderedPrompt r = RenderCustomPrompt(raw, Language::English, Language::Chinese);
    Check(r.ok && r.text == want, "custom render: whitespace, CRLF and Unicode preserved");
    r = RenderCustomPrompt(" x ", Language::English, Language::Chinese);
    Check(r.ok && r.text == " x ", "custom render: no trimming of a non-blank template");
    r = RenderCustomPrompt("Only say pong.", Language::English, Language::Chinese);
    Check(r.ok && r.text == "Only say pong.", "custom render: no default instruction appended");
    // Blank = empty or Unicode-whitespace-only, the same definition profiles::Validate uses
    // (U+3000 ideographic space and U+00A0 included). The saved text itself is never edited.
    for (const char* blank : {"", " ", "\t\r\n  ", "\xE3\x80\x80", "\xC2\xA0 \xC2\xA0"}) {
        r = RenderCustomPrompt(blank, Language::English, Language::Chinese);
        Check(!r.ok, "custom render: blank template is not ok");
    }
    r = RenderCustomPrompt("\xE3\x80\x80x", Language::English, Language::Chinese);
    Check(r.ok && r.text == "\xE3\x80\x80x", "custom render: leading Unicode space kept on a non-blank template");
}

void TestRenderLiteralTokens() {
    const char* const shaped[] = {"{{foo}}", "{{Source_Language}}", "{{TARGET_LANGUAGE}}",
                                  "{{ source_language }}", "{{target-language}}"};
    for (const char* literal : shaped) {
        const RenderedPrompt r = RenderCustomPrompt(literal, Language::English, Language::Chinese);
        Check(r.ok && r.text == literal, "custom render: unknown {{name}} stays literal");
        Check(!r.warnings.empty() && r.warnings[0] == execution::kWarningUnknownPlaceholder,
              "custom render: unknown {{name}} warns with the fixed code");
        Check(r.unknownTokens.size() == 1 && r.unknownTokens[0] == literal,
              "custom render: unknown token reported for UI memory only");
    }
    const char* const stray[] = {"{source_language}", "{{source_language}", "{{target_language}",
                                 "{target_language}}", "{{{x", "}}", "{", "}{", "{{}}"};
    for (const char* literal : stray) {
        const RenderedPrompt r = RenderCustomPrompt(literal, Language::English, Language::Chinese);
        Check(r.ok && r.text == literal, "custom render: stray braces stay literal text");
    }
}

void TestRenderSinglePass() {
    RenderedPrompt r =
        RenderCustomPrompt("{{{{target_language}}}}", Language::English, Language::Chinese);
    Check(r.ok && r.text == "{{Chinese}}", "custom render: outer braces are plain text");
    r = RenderCustomPrompt("{{target_{{target_language}}}}", Language::English, Language::Chinese);
    Check(r.ok && r.text == "{{target_Chinese}}", "custom render: single pass, no recursion");
    r = RenderCustomPrompt("{{source_language}}{{target_language}}", Language::English,
                           Language::Chinese);
    Check(r.ok && r.text == "EnglishChinese", "custom render: adjacent tokens");
}

void TestRenderWarningPrivacy() {
    const RenderedPrompt r =
        RenderCustomPrompt("{{secret_name_xyz}} body", Language::English, Language::Chinese);
    Check(!r.warnings.empty(), "custom render: unknown token produces a warning");
    for (const std::string& warning : r.warnings) {
        Check(warning == execution::kWarningUnknownPlaceholder &&
                  warning.find("secret_name_xyz") == std::string::npos &&
                  warning.find("{{") == std::string::npos,
              "custom render: warning is a fixed code with no token fragment");
    }
}

} // namespace

void RunPresetTests() {
    TestSystemGoldens();
    TestPlainGoldens();
    TestPlainTextVerbatim();
    TestEveryPromptName();
}

void RunRenderTests() {
    TestRenderKnownTokens();
    TestRenderUnknownSource();
    TestRenderWhitespaceAndBlank();
    TestRenderLiteralTokens();
    TestRenderSinglePass();
    TestRenderWarningPrivacy();
}

} // namespace execution_test
