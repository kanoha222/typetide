cmake_minimum_required(VERSION 3.20)
project(TypeTideProfileCoreTests LANGUAGES CXX)

option(PROFILE_CORE_JSON_ONLY
    "Run JSON boundary tests before the profile/store implementation exists" OFF)

add_executable(ProfileCoreSelfTest
    ../src/Json.cpp
    ../src/Language.cpp
    ../src/TranslationTypes.cpp
    ../src/TranslationProfile.cpp
    ../src/TranslationProfileStore.cpp
    ../src/ProfileSelfTest.cpp
)

target_compile_features(ProfileCoreSelfTest PRIVATE cxx_std_20)
set_target_properties(ProfileCoreSelfTest PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(ProfileCoreSelfTest PRIVATE TYPETIDE_PROFILE_CORE_TEST_MAIN)

if(PROFILE_CORE_JSON_ONLY)
    target_compile_definitions(ProfileCoreSelfTest PRIVATE TYPETIDE_JSON_SELF_TEST_ONLY)
endif()

if(MSVC)
    target_compile_options(ProfileCoreSelfTest PRIVATE /utf-8 /EHsc /W4)
    # Baseline Json.cpp uses these headers without including them. Keep the
    # compatibility workaround test-only so production stays unchanged for RED.
    target_compile_options(ProfileCoreSelfTest PRIVATE /FIcstring /FIcstdlib)
endif()

# After observing behavioral RED, the core owner can add the implemented
# TranslationTypes/Profile/Store and Language sources, then disable JSON-only.

set(SETTINGS_TRANSACTION_SOURCES
    ../src/Settings.cpp
    ../src/SettingsCodec.cpp
    ../src/SettingsCodecGlobals.cpp
    ../src/SettingsIdentifier.cpp
    ../src/SettingsStorage.cpp
    ../src/SettingsTransaction.cpp
)

add_executable(NativeStorageSelfTest
    ../src/Json.cpp
    ../src/Language.cpp
    ../src/Log.cpp
    ../src/TranslationTypes.cpp
    ../src/TranslationProfile.cpp
    ../src/TranslationProfileStore.cpp
    ${SETTINGS_TRANSACTION_SOURCES}
    TestCredentialsDouble.cpp
    NativeStorageFixture.cpp
    NativeStorageSelfTest.cpp
)

target_compile_features(NativeStorageSelfTest PRIVATE cxx_std_20)
set_target_properties(NativeStorageSelfTest PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(NativeStorageSelfTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(NativeStorageSelfTest PRIVATE /utf-8 /EHsc /W4)
    target_compile_options(NativeStorageSelfTest PRIVATE /FIcstring /FIcstdlib)
endif()

target_link_libraries(NativeStorageSelfTest PRIVATE
    user32
    shell32
    ole32
    advapi32
)

add_executable(NativeTransactionSelfTest
    ../src/Json.cpp
    ../src/Language.cpp
    ../src/Log.cpp
    ../src/TranslationTypes.cpp
    ../src/TranslationProfile.cpp
    ../src/TranslationProfileStore.cpp
    ${SETTINGS_TRANSACTION_SOURCES}
    NativeStorageFixture.cpp
    TestCredentialFacade.cpp
    TestCredentialStoreSeam.cpp
    NativeTransactionSupport.cpp
    NativeTransactionMigrationTests.cpp
    NativeTransactionAdditionalTests.cpp
    NativeTransactionSelfTest.cpp
)

target_compile_features(NativeTransactionSelfTest PRIVATE cxx_std_20)
set_target_properties(NativeTransactionSelfTest PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(NativeTransactionSelfTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(NativeTransactionSelfTest PRIVATE /utf-8 /EHsc /W4)
    target_compile_options(NativeTransactionSelfTest PRIVATE /FIcstring /FIcstdlib)
endif()

# No production Credentials.cpp or WinCred import library in this target.
# TestCredentialFacade.cpp supplies every credentials::* symbol at link time.
target_link_libraries(NativeTransactionSelfTest PRIVATE
    user32
    shell32
    ole32
)

# Pure translation-execution helpers (prompts, request bodies, endpoint policy, cache
# identity). Deliberately NO Settings/Credentials/Log/GUI/BuiltInModel sources and no
# production Translator.cpp/Http.cpp: the target builds only the Core value modules plus
# the new execution modules. winhttp is linked solely for the offline WinHttpCrackUrl
# parse in HttpUrl.cpp; no session, connection or request is ever created.
add_executable(TranslationExecutionSelfTest
    ../src/Json.cpp
    ../src/Language.cpp
    ../src/TranslationTypes.cpp
    ../src/TranslationProfile.cpp
    ../src/TranslationProfileStore.cpp
    ../src/HttpUrl.cpp
    ../src/EndpointPolicy.cpp
    ../src/TranslationExecution.cpp
    ../src/TranslationSession.cpp
    ../src/ProfileEditor.cpp
    TranslationExecutionSupport.cpp
    TranslationExecutionPromptTests.cpp
    TranslationExecutionDirectionTests.cpp
    TranslationExecutionRequestTests.cpp
    TranslationExecutionClassifierTests.cpp
    TranslationExecutionEndpointTests.cpp
    TranslationExecutionCacheTests.cpp
    TranslationExecutionRuntimeTests.cpp
    TranslationSessionTests.cpp
    ProfileEditorTests.cpp
    TranslationExecutionSelfTest.cpp
)

target_compile_features(TranslationExecutionSelfTest PRIVATE cxx_std_20)
set_target_properties(TranslationExecutionSelfTest PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(TranslationExecutionSelfTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(TranslationExecutionSelfTest PRIVATE /utf-8 /EHsc /W4)
    # Same test-only Json.cpp header workaround as the other standalone targets.
    target_compile_options(TranslationExecutionSelfTest PRIVATE /FIcstring /FIcstdlib)
endif()

target_link_libraries(TranslationExecutionSelfTest PRIVATE winhttp)
