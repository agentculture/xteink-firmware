add_executable(ReaderLongPressTest ReaderLongPressTest.cpp)

target_link_libraries(ReaderLongPressTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderLongPressTest)
