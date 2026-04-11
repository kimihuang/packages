# SLT下位机系统使用说明

## 系统概述

SLT (System Level Test) 下位机系统用于从串口接收上位机发送的测试命令，解析并执行Shell命令，然后分析执行日志，根据模式匹配返回测试结果。

### 主要功能
1. **串口通信**：接收上位机命令，返回测试结果
2. **命令解析**：解析特定格式的SLT测试命令
3. **命令执行**：执行Shell命令并监控超时
4. **日志分析**：收集日志并进行模式匹配
5. **结果返回**：返回匹配结果(0表示成功，-1表示失败)

## 快速开始

### 1. 构建系统

```bash
# 方法1: 使用Makefile (推荐)
make release

# 方法2: 使用构建脚本
./build.sh release

# 方法3: 使用CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 2. 运行测试

```bash
# 运行单元测试
make test

# 或直接运行测试程序
./build/bin/slt_test
```

### 3. 运行主程序

```bash
# 查看帮助
./build/bin/slt_daemon --help

# 使用默认配置运行
./build/bin/slt_daemon

# 指定串口和波特率
./build/bin/slt_daemon --port /dev/ttyUSB0 --baud 115200 --verbose

# 使用配置文件
./build/bin/slt_daemon --config slt_config_example.yaml
```

## 系统架构

### 目录结构
```
linux-slt/
├── inc/                    # 头文件目录
│   ├── slt_interfaces.h   # 核心接口定义
│   ├── slt_utils.h        # 工具函数
│   ├── slt_command_parser.h
│   ├── linux_serial_port.h
│   ├── system_command_executor.h
│   ├── pattern_log_processor.h
│   ├── slt_controller.h
│   └── test_mock_serial_port.h
├── src/                    # 源文件目录
│   ├── main.cpp           # 主程序
│   ├── test_main.cpp      # 测试程序
│   ├── slt_utils.cpp
│   ├── slt_command_parser.cpp
│   ├── linux_serial_port.cpp
│   ├── system_command_executor.cpp
│   ├── pattern_log_processor.cpp
│   ├── slt_controller.cpp
│   ├── test_command.cpp
│   ├── execution_result.cpp
│   ├── match_result.cpp
│   ├── slt_config.cpp
│   ├── component_factory.cpp
│   └── test_mock_serial_port.cpp
├── CMakeLists.txt         # CMake构建配置
├── Makefile               # 简化构建
├── build.sh               # 构建脚本
├── slt_config_example.yaml # 配置文件示例
├── slt_design.puml        # 系统类图
├── slt_architecture.md    # 架构设计文档
└── README.md              # 项目说明
```

### 核心组件

1. **SLTController** - 核心控制器，协调所有组件
2. **SerialPortHandler** - 串口通信接口
3. **CommandParser** - SLT命令解析器
4. **CommandExecutor** - Shell命令执行器
5. **LogProcessor** - 日志处理和模式匹配器
6. **TestCommand** - 命令数据模型
7. **ExecutionResult** - 执行结果数据模型

## 命令格式

### SLT命令格式
```
slt_test_cmd_start:$$
<测试命令>
slt_test_cmd_end:^^
```

### 测试命令语法
```
<测试名称> [参数] [参数] ...
```

### 参数说明
- `-v` : verbose模式，打印详细信息
- `-c <命令>` : 要执行的Shell命令
- `-p <模式>` : 匹配日志的模式
- `-t <超时>` : 超时时间(毫秒)

### 示例命令
```
ACPU_TEST_0001 -v -c "echo 'Hello, SLT!'" -p "Hello" -t 1000
```

## 配置文件

### 配置示例
```yaml
# slt_config_example.yaml
serial:
  port: "/dev/ttyUSB0"
  baud_rate: 115200

logging:
  directory: "/var/log/slt/"

commands:
  default_timeout_ms: 5000
  markers:
    cmd_start: "$$"
    cmd_end: "^^"
```

### 配置参数说明
- **serial.port** : 串口设备路径
- **serial.baud_rate** : 串口波特率
- **logging.directory** : 日志存储目录
- **commands.default_timeout_ms** : 默认命令超时时间
- **commands.markers** : 命令标记符

## API使用示例

### C++ API
```cpp
#include "slt_interfaces.h"
#include "slt_controller.h"
#include <iostream>

int main() {
    // 创建配置
    slt::SLTConfig config;
    config.setSerialPort("/dev/ttyUSB0");
    config.setBaudRate(115200);
    config.setLogDirectory("/var/log/slt/");
    
    // 创建控制器
    auto controller = slt::ComponentFactory::createController(config);
    
    // 启动控制器
    if (controller->start()) {
        std::cout << "控制器启动成功" << std::endl;
        
        // 处理命令
        auto result = controller->processNextCommand();
        
        // 输出结果
        std::cout << "命令ID: " << result.getCommandId() << std::endl;
        std::cout << "退出码: " << result.getExitCode() << std::endl;
        std::cout << "模式匹配: " << (result.isPatternMatched() ? "成功" : "失败") << std::endl;
        
        // 停止控制器
        controller->stop();
    }
    
    return 0;
}
```

## 测试程序

### 运行测试
```bash
# 运行所有测试
./build/bin/slt_test

# 测试输出示例
=== SLT下位机系统单元测试 ===
=== 测试命令解析器 ===
✓ 测试用例1通过: 正常命令解析
✓ 测试用例2通过: 不带verbose的命令解析
✓ 测试用例3通过: 无效命令格式检测
✓ 测试用例4通过: 短参数组合解析
=== 命令解析器测试完成 ===
...
=== 所有测试通过 ===
```

### 测试覆盖范围
1. **命令解析器测试** : 验证SLT命令解析功能
2. **命令执行器测试** : 验证Shell命令执行功能
3. **日志处理器测试** : 验证日志处理和模式匹配
4. **集成测试** : 验证完整处理流程
5. **SLT命令格式测试** : 验证与标准格式的兼容性

## 开发说明

### 添加新组件
1. 在`inc/`目录创建头文件
2. 在`src/`目录创建实现文件
3. 在`CMakeLists.txt`中添加源文件
4. 在`test_main.cpp`中添加测试用例

### 扩展命令格式
1. 修改`SLTCommandParser`类
2. 扩展`TestCommand`数据模型
3. 更新测试用例

### 添加新的日志处理器
1. 实现`LogProcessor`接口
2. 在`ComponentFactory`中添加创建方法
3. 更新配置文件支持

## 故障排除

### 常见问题

#### 1. 串口无法打开
- 检查串口设备路径是否正确
- 检查用户是否有串口访问权限
```bash
# 添加用户到dialout组
sudo usermod -a -G dialout $USER
```

#### 2. 命令执行超时
- 检查命令是否正确
- 调整超时时间配置
- 检查系统资源使用情况

#### 3. 模式匹配失败
- 检查模式是否正确
- 查看生成的日志文件
- 尝试使用简单字符串模式

#### 4. 构建失败
- 检查CMake版本
- 检查C++编译器
- 检查依赖库

### 调试模式
```bash
# 启用详细输出
./build/bin/slt_daemon --verbose

# 调试构建
make debug
```

## 性能优化

### 配置建议
1. **串口配置** : 根据实际硬件调整波特率
2. **超时配置** : 根据命令执行时间调整超时
3. **日志配置** : 定期清理日志文件
4. **并发配置** : 根据系统资源调整并发数

### 内存管理
1. 使用智能指针管理资源
2. 实现对象池减少内存分配
3. 使用移动语义避免拷贝

## 部署指南

### 系统要求
- Linux系统 (Ubuntu/CentOS等)
- C++17兼容编译器
- 串口设备权限
- 足够的磁盘空间存储日志

### 安装步骤
```bash
# 1. 构建项目
make release

# 2. 安装到系统
sudo make install

# 3. 创建配置文件
sudo cp slt_config_example.yaml /etc/slt/config.yaml

# 4. 创建日志目录
sudo mkdir -p /var/log/slt/
sudo chown $USER:$USER /var/log/slt/

# 5. 创建系统服务
sudo cp slt.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable slt.service
sudo systemctl start slt.service
```

### 系统服务配置
```ini
# slt.service
[Unit]
Description=SLT Test Daemon
After=network.target

[Service]
Type=simple
User=sltuser
Group=sltgroup
WorkingDirectory=/opt/slt/
ExecStart=/usr/bin/slt_daemon --config /etc/slt/config.yaml
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
```

## 许可证

本项目采用MIT许可证。详细信息请查看LICENSE文件。

## 支持与反馈

如有问题或建议，请：
1. 查看项目文档
2. 运行测试程序验证功能
3. 检查系统日志
4. 提交Issue报告问题