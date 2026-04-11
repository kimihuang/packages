// 测试用模拟串口头文件
// 文件名: test_mock_serial_port.h

#ifndef TEST_MOCK_SERIAL_PORT_H
#define TEST_MOCK_SERIAL_PORT_H

#include "slt_interfaces.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>

namespace slt {
namespace test {

class MockSerialPort : public SerialPortHandler {
public:
    MockSerialPort();
    ~MockSerialPort() override;
    
    // SerialPortHandler接口实现
    bool open(const std::string& portName, int baudRate) override;
    std::string readData(int timeoutMs) override;
    bool writeData(const std::string& data) override;
    void close() override;
    
    bool isOpen() const override;
    std::string getLastError() const override;
    
    // 测试专用方法
    void pushMockData(const std::string& data);
    std::string getLastWrittenData() const;
    void clear();
    
private:
    mutable std::mutex mutex_;
    std::condition_variable condVar_;
    std::queue<std::string> dataQueue_;
    std::string lastWrittenData_;
    bool isOpen_;
    std::string lastError_;
};

} // namespace test
} // namespace slt

#endif // TEST_MOCK_SERIAL_PORT_H