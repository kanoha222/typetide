#pragma once

// Golden pieces transcribed BY HAND from the CURRENT Root builders
// (translator::SystemPrompt / PlainPrompt in Translator.cpp and the style strings in
// Language.cpp). They are never produced by the code under test.
#define GOLD_SYS_HEAD "You are a professional translation engine. Translate the user's text from "
#define GOLD_SYS_TAIL \
    " Output ONLY the translation, with no quotes, no explanations, no extra notes. " \
    "Preserve the original meaning and formatting."
#define GOLD_PLAIN_HEAD "Translate the following text from "
#define GOLD_PLAIN_TAIL " Only output the translation, no explanation.\n\n"

#define GOLD_STYLE_FORMAL " Use a professional, formal tone."
#define GOLD_STYLE_CASUAL " Use a natural, casual, conversational tone."
#define GOLD_STYLE_POLISHED \
    " Improve clarity and flow so it reads polished and native, while keeping the original meaning."

// Fully spelled anchors (no macro reuse) for the Faithful / known-source shape.
#define GOLD_SYS_EN_ZH_FAITHFUL \
    "You are a professional translation engine. Translate the user's text from English into " \
    "Chinese. Output ONLY the translation, with no quotes, no explanations, no extra notes. " \
    "Preserve the original meaning and formatting."
#define GOLD_PLAIN_EN_ZH_HELLO \
    "Translate the following text from English to Chinese. Only output the translation, " \
    "no explanation.\n\nHello"
