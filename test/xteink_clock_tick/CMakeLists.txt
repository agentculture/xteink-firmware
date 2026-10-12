add_executable(XteinkClockTickTest
  XteinkClockTickTest.cpp
)

target_include_directories(XteinkClockTickTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(XteinkClockTickTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XteinkClockTickTest)
