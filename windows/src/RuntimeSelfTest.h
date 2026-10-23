//
//  RuntimeSelfTest.h — `TypeTide.exe --selftest-runtime`: profile execution through the real
//  Translator + WinHTTP path against the local mock provider (windows/scripts/mock-openai.ps1).
//  Needs TYPETIDE_MOCK_PORT, TYPETIDE_MOCK_RECORD and an isolated TYPETIDE_DATA_DIR.
//
#pragma once

/// 0 = all passed, 1 = failures, 2 = missing test environment.
int RunRuntimeSelfTests();
