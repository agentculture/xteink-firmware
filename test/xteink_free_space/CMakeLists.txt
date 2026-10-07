add_executable(XteinkFreeSpaceTest XteinkFreeSpaceTest.cpp)

target_link_libraries(XteinkFreeSpaceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XteinkFreeSpaceTest)
