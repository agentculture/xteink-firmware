add_executable(ZoomModeTest ZoomModeTest.cpp)

target_link_libraries(ZoomModeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ZoomModeTest)
