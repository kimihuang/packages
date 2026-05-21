cmake_minimum_required(VERSION 3.10)
project(slt_system VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 注意: 不设置自定义OUTPUT_DIRECTORY, 让CMake使用默认值
# 默认情况下所有产物输出到 ${CMAKE_BINARY_DIR} (即 $@D/build/)
# 这样Buildroot cmake-package能可靠地定位产物位置

# RPATH 设置 (Buildroot 交叉编译必须)
# 构建阶段使用构建树RPATH以便测试
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
# 安装阶段写入正确的运行时RPATH
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib:/usr/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/inc)

# 编译选项
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -Wpedantic -Wconversion)
    add_compile_options(-Wno-unused-parameter)  # 允许未使用参数
endif()

# 查找需要的库
find_package(Threads REQUIRED)

# 创建SLT库
add_library(slt_system SHARED
    src/slt_utils.cpp
    src/test_command.cpp
    src/execution_result.cpp
    src/match_result.cpp
    src/slt_command_parser.cpp
    src/linux_serial_port.cpp
    src/tcp_server_handler.cpp
    src/system_command_executor.cpp
    src/pattern_log_processor.cpp
    src/slt_controller.cpp
    src/component_factory.cpp
    src/slt_config.cpp
)

target_link_libraries(slt_system
    PRIVATE
        Threads::Threads
)

# 创建主程序
add_executable(slt_daemon
    src/main.cpp
)

target_link_libraries(slt_daemon
    PRIVATE
        slt_system
)

# 创建测试程序
add_executable(slt_test
    src/test_main.cpp
    src/test_mock_serial_port.cpp
)

target_link_libraries(slt_test
    PRIVATE
        slt_system
)

# 安装配置
install(TARGETS slt_system DESTINATION lib)
install(TARGETS slt_daemon DESTINATION bin)
install(TARGETS slt_test DESTINATION bin)
install(DIRECTORY inc/ DESTINATION include/slt)
install(FILES slt_config_example.yaml DESTINATION etc/slt)

# 测试配置
enable_testing()
add_test(NAME slt_system_test COMMAND slt_test)