add_executable(XteinkWifiJoinTest
  XteinkWifiJoinTest.cpp
  ${REPO_ROOT}/src/xteink/WifiJoin.cpp
)

target_include_directories(XteinkWifiJoinTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(XteinkWifiJoinTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XteinkWifiJoinTest)
