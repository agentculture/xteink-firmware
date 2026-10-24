# ArduinoJson is header-only; fetch the version pinned in platformio.ini.
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

add_executable(XteinkProvisioningTest
  XteinkProvisioningTest.cpp
  ${REPO_ROOT}/src/xteink/ProvisioningProtocol.cpp
)

target_include_directories(XteinkProvisioningTest PRIVATE
  ${REPO_ROOT}/src
  ${arduinojson_SOURCE_DIR}/src
)

target_link_libraries(XteinkProvisioningTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XteinkProvisioningTest)
