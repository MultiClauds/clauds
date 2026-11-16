# SPDX-License-Identifier: MIT
find_package(Qt6 REQUIRED COMPONENTS Test)

foreach(name SessionLinkerTest UsageTest AutoSwitchTest ClaudeProcessTest AccountStoreTest)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE clauds-core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
