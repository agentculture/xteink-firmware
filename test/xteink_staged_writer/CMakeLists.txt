add_executable(XteinkStagedWriterTest XteinkStagedWriterTest.cpp)

target_link_libraries(XteinkStagedWriterTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(XteinkStagedWriterTest)
