add_executable(LineWindowTest LineWindowTest.cpp)

target_link_libraries(LineWindowTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LineWindowTest)
