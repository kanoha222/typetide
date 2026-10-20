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

add_executable(NativeStorageSelfTest
    ../src/Json.cpp
    ../src/Language.cpp
    ../src/Log.cpp
    ../src/Settings.cpp
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
