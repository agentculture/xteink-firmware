add_executable(ResetBootTest ResetBootTest.cpp)

target_link_libraries(ResetBootTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ResetBootTest)
