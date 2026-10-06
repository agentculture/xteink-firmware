add_executable(TabListNavTest TabListNavTest.cpp)

target_link_libraries(TabListNavTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TabListNavTest)
