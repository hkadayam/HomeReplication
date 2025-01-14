cmake_minimum_required(VERSION 3.13)

include_directories (BEFORE ../include/)
include_directories (BEFORE ../lib/)
include_directories (BEFORE .)

find_package(GTest REQUIRED)

link_directories(${spdk_LIB_DIRS} ${dpdk_LIB_DIRS})

add_executable(test_home_raft_log_store)
target_sources(test_home_raft_log_store PRIVATE test_home_raft_log_store.cpp)
target_link_libraries(test_home_raft_log_store
            home_replication
            ${COMMON_TEST_DEPS}
            GTest::gmock
        )
add_test(NAME HomeRaftLogStore COMMAND ${CMAKE_BINARY_DIR}/bin/test_home_raft_log_store)

add_executable(test_home_sm_store)
target_sources(test_home_sm_store PRIVATE test_home_sm_store.cpp)
target_link_libraries(test_home_sm_store home_replication ${COMMON_TEST_DEPS} GTest::gmock)
add_test(NAME HomeStateStore COMMAND ${CMAKE_BINARY_DIR}/bin/test_home_sm_store)
