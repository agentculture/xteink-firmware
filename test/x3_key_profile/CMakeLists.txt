add_executable(X3KeyProfileTest X3KeyProfileTest.cpp)

target_link_libraries(X3KeyProfileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(X3KeyProfileTest)
