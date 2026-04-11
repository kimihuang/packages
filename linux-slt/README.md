# SLT (System Level Test) 下位机系统

## 项目概述

SLT下位机系统是一个用于从串口接收上位机发送的测试命令，解析并执行Shell命令，然后分析执行日志并返回测试结果的系统。系统按照类图设计实现，具有高内聚、低耦合的架构特点。

## 系统架构

### 设计原则
- **高内聚低耦合**：每个组件职责明确，通过接口通信
- **易于扩展**：支持插件化架构，可添加新的命令处理器、日志处理器等
- **容错性强**：完善的错误处理机制，支持异常恢复
- **性能优化**：支持异步处理，内存池优化

### 核心组件
1. **SLTController** - 核心控制器，协调所有组件
2. **SerialPortHandler** - 串口通信接口
3. **CommandParser** - SLT命令解析器
4. **CommandExecutor** - Shell命令执行器
5. **LogProcessor** - 日志处理和模式匹配器

### 数据模型
- **TestCommand** - 测试命令数据模型
- **ExecutionResult** - 执行结果数据模型
- **MatchResult** - 模式匹配结果数据模型

## 快速开始

### 构建系统
```bash
# 使用Makefile (推荐)
make release

# 或使用构建脚本
./build.sh release
```

### 运行测试
```bash
# 运行单元测试
make test

# 测试输出示例
./build/bin/slt_test
```

### 运行主程序
```bash
# 查看帮助
./build/bin/slt_daemon --help

# 运行程序
./build/bin/slt_daemon --port /dev/ttyUSB0 --baud 115200 --verbose
```

## 命令格式

### SLT命令格式
```
slt_test_cmd_start:$$
<测试命令>
slt_test_cmd_end:^^
```

### 示例命令
```
ACPU_TEST_0001 -v -c "echo 'Hello, SLT!'" -p "Hello" -t 1000
```

### 参数说明
- `-v` : verbose模式，打印详细信息
- `-c <命令>` : 要执行的Shell命令
- `-p <模式>` : 匹配日志的模式
- `-t <超时>` : 超时时间(毫秒)

## 项目结构

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
├── slt_design.puml        # PlantUML类图
├── slt_simple_design.puml # 简化类图
├── slt_architecture.md    # 架构设计文档
├── USAGE.md              # 详细使用说明
└── README.md             # 项目说明
```

## 依赖项

### 必需依赖
- C++17兼容编译器 (g++ >= 7 或 clang++ >= 5)
- CMake >= 3.10
- Linux系统 (需要串口支持)

### 可选依赖
- yaml-cpp (用于配置文件解析)
- Graphviz (用于生成PlantUML图表)

## 构建选项

### 调试构建
```bash
make debug
```

### 发布构建
```bash
make release
```

### 完整构建流程
```bash
make all
```

### 清理构建
```bash
make clean
```

## 配置文件

系统支持YAML格式的配置文件，示例见`slt_config_example.yaml`：

```yaml
serial:
  port: "/dev/ttyUSB0"
  baud_rate: 115200

logging:
  directory: "/var/log/slt/"

commands:
  default_timeout_ms: 5000
```

## API使用示例

```cpp
#include "slt_interfaces.h"
#include <iostream>

int main() {
    // 创建配置
    slt::SLTConfig config;
    config.setSerialPort("/dev/ttyUSB0");
    config.setBaudRate(115200);
    
    // 创建控制器
    auto controller = slt::ComponentFactory::createController(config);
    
    // 启动并运行
    if (controller->start()) {
        auto result = controller->processNextCommand();
        std::cout << "命令处理结果: " << result.getExitCode() << std::endl;
        controller->stop();
    }
    
    return 0;
}
```

## 测试验证

### 测试用例覆盖
1. **命令解析测试** : 验证SLT命令解析功能
2. **命令执行测试** : 验证Shell命令执行功能
3. **日志处理测试** : 验证日志处理和模式匹配
4. **集成测试** : 验证完整处理流程
5. **SLT命令格式测试** : 验证与标准格式的兼容性

### 运行测试
```bash
# 运行所有测试
./build/bin/slt_test

# 预期输出
=== SLT下位机系统单元测试 ===
✓ 所有测试通过
```

## 故障排除

### 常见问题

1. **串口权限问题**
```bash
# 添加用户到dialout组
sudo usermod -a -G dialout $USER
# 重新登录生效
```

2. **构建失败**
- 检查CMake版本: `cmake --version`
- 检查编译器: `g++ --version`
- 检查依赖库

3. **运行错误**
- 检查串口设备是否存在: `ls -l /dev/ttyUSB*`
- 检查配置文件路径
- 查看程序输出日志

### 调试模式
```bash
# 启用详细输出
./build/bin/slt_daemon --verbose

# 使用调试构建
make debug
./build/bin/slt_daemon
```

## 扩展开发

### 添加新组件
1. 实现相应的接口类
2. 在ComponentFactory中添加创建方法
3. 更新CMakeLists.txt
4. 添加测试用例

### 扩展命令格式
1. 修改SLTCommandParser类
2. 扩展TestCommand数据模型
3. 更新配置文件解析

## 性能优化建议

1. **串口配置优化**
   - 根据硬件调整波特率
   - 优化缓冲区大小
   - 调整读写超时

2. **命令执行优化**
   - 使用异步执行
   - 实现命令队列
   - 优化资源管理

3. **日志处理优化**
   - 实现日志轮转
   - 压缩历史日志
   - 优化模式匹配算法

## 许可证

本项目采用MIT许可证。

## 贡献指南

1. Fork项目
2. 创建功能分支
3. 提交更改
4. 推送到分支
5. 创建Pull Request

## 联系方式

如有问题或建议，请提交Issue或通过项目页面联系。

---

**项目状态**: 已完成设计和实现，通过所有测试用例

**最后更新**: 2025年4月