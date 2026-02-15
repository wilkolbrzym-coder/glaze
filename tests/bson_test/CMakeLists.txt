add_executable(bson_test bson_test.cpp)
target_link_libraries(bson_test PRIVATE glz_test_common)
add_test(NAME bson_test COMMAND bson_test)
