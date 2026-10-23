#include "TranslationExecutionSupport.h"

// Action -> (source, target) resolution. Expected values are derived by hand
// from the approved rules and the language samples, not from the helper.
namespace execution_test {
namespace {

using execution::ExecutionDirection;
using execution::ResolveActionDirection;

bool Is(const ExecutionDirection& d, std::optional<Language> source, Language target) {
    return d.ready && d.source == source && d.target == target;
}

TranslationProfile PairProfile(TranslationDirection direction) {
    TranslationProfile profile = MakeProfile(TranslationBackend::Ollama);
    profile.direction = direction;
    return profile;
}

void TestNativeForeignActions() {
    const TranslationProfile p = PairProfile(TranslationDirection::Auto);
    Check(Is(ResolveActionDirection(p, ShortcutAction::NativePopup, kEnglishText),
             Language::English, Language::Chinese),
          "native popup: target = native, source = Detect");
    Check(Is(ResolveActionDirection(p, ShortcutAction::NativePopup, kChineseText),
             Language::Chinese, Language::Chinese),
          "native popup: source stays Detect even when equal to the target");
    Check(Is(ResolveActionDirection(p, ShortcutAction::ForeignPopup, kChineseText),
             Language::Chinese, Language::English),
          "foreign popup: target = foreign");
    Check(Is(ResolveActionDirection(p, ShortcutAction::ForeignReplace, kRussianText),
             Language::Russian, Language::English),
          "foreign replace: a detected third language stays the source");
    Check(Is(ResolveActionDirection(p, ShortcutAction::NativeReplace, kUndetectable),
             std::nullopt, Language::Chinese),
          "native replace: undetectable source stays nullopt");
}

void TestSmartActions() {
    const TranslationProfile p = PairProfile(TranslationDirection::Auto);
    Check(Is(ResolveActionDirection(p, ShortcutAction::SmartPopup, kEnglishText),
             Language::English, Language::Chinese), "smart popup: foreign text -> native");
    Check(Is(ResolveActionDirection(p, ShortcutAction::SmartReplace, kEnglishText),
             Language::English, Language::Chinese), "smart replace: foreign text -> native");
    Check(Is(ResolveActionDirection(p, ShortcutAction::SmartPopup, kChineseText),
             Language::Chinese, Language::English), "smart popup: native text -> foreign");
    Check(Is(ResolveActionDirection(p, ShortcutAction::SmartPopup, kRussianText),
             Language::Russian, Language::Chinese),
          "smart popup: third language falls back to native");
    Check(Is(ResolveActionDirection(p, ShortcutAction::SmartReplace, kRussianText),
             Language::Russian, Language::English),
          "smart replace: third language falls back to foreign");
    Check(Is(ResolveActionDirection(p, ShortcutAction::SmartPopup, kUndetectable),
             std::nullopt, Language::Chinese), "smart popup: unknown source -> native");
    Check(Is(ResolveActionDirection(p, ShortcutAction::SmartReplace, kUndetectable),
             std::nullopt, Language::English), "smart replace: unknown source -> foreign");
}

void TestFixedProfileDirection() {
    Check(Is(ResolveActionDirection(PairProfile(TranslationDirection::NativeToForeign),
                                    ShortcutAction::SmartPopup, kRussianText),
             Language::Chinese, Language::English),
          "smart + nativeToForeign: profile direction wins over detection");
    Check(Is(ResolveActionDirection(PairProfile(TranslationDirection::ForeignToNative),
                                    ShortcutAction::SmartReplace, kChineseText),
             Language::English, Language::Chinese),
          "smart + foreignToNative: profile direction wins over detection");
    Check(Is(ResolveActionDirection(PairProfile(TranslationDirection::ForeignToNative),
                                    ShortcutAction::ForeignReplace, kChineseText),
             Language::Chinese, Language::English),
          "foreign action overrides the profile direction (target foreign, source Detect)");
    Check(Is(ResolveActionDirection(PairProfile(TranslationDirection::NativeToForeign),
                                    ShortcutAction::NativePopup, kRussianText),
             Language::Russian, Language::Chinese),
          "native action overrides the profile direction (target native, source Detect)");
}

void TestOtherLanguagePair() {
    TranslationProfile p = PairProfile(TranslationDirection::Auto);
    p.nativeLanguage = Language::Spanish;
    p.foreignLanguage = Language::French;
    Check(Is(ResolveActionDirection(p, ShortcutAction::NativePopup, kUndetectable),
             std::nullopt, Language::Spanish), "native action uses the profile's own native language");
    Check(Is(ResolveActionDirection(p, ShortcutAction::ForeignPopup, kUndetectable),
             std::nullopt, Language::French), "foreign action uses the profile's own foreign language");
}

void TestTargetOverride() {
    const TranslationProfile p = PairProfile(TranslationDirection::Auto);
    const TranslationProfile before = p;
    Check(Is(ResolveActionDirection(p, ShortcutAction::SmartPopup, kEnglishText, Language::Russian),
             Language::English, Language::Russian),
          "override: replaces only the target, source keeps Detect");
    Check(Is(ResolveActionDirection(PairProfile(TranslationDirection::NativeToForeign),
                                    ShortcutAction::SmartPopup, kEnglishText, Language::Spanish),
             Language::Chinese, Language::Spanish),
          "override: fixed-direction source is the saved native, not re-detected");
    Check(Is(ResolveActionDirection(p, ShortcutAction::ForeignPopup, kUndetectable, Language::French),
             std::nullopt, Language::French),
          "override: unknown source stays unknown");
    Check(p == before, "override never edits the saved profile");
}

} // namespace

void RunDirectionTests() {
    TestNativeForeignActions();
    TestSmartActions();
    TestFixedProfileDirection();
    TestOtherLanguagePair();
    TestTargetOverride();
}

} // namespace execution_test
