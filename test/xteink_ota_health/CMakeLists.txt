# ArduinoJson is header-only; fetch the version pinned in platformio.ini
# (shared with other xteink tests: populate once).
include(FetchContent)
FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
)
FetchContent_GetProperties(arduinojson)
if(NOT arduinojson_POPULATED)
  FetchContent_Populate(arduinojson)
endif()

add_executable(XteinkOtaHealthTest
  XteinkOtaHealthTest.cpp
  ${REPO_ROOT}/src/xteink/OtaHealth.cpp
  ${REPO_ROOT}/src/xteink/SyncProtocol.cpp
  ${REPO_ROOT}/src/util/StringUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(XteinkOtaHealthTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
  ${arduinojson_SOURCE_DIR}/src
)

target_link_libraries(XteinkOtaHealthTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XteinkOtaHealthTest)
