// Linux串口处理器头文件
// 文件名: linux_serial_port.h

#ifndef LINUX_SERIAL_PORT_H
#define LINUX_SERIAL_PORT_H

#include "slt_interfaces.h"
#include <string>
#include <atomic>

namespace slt {

class LinuxSerialPort : public SerialPortHandler {
public:
    LinuxSerialPort();
    ~LinuxSerialPort() override;
    
    // SerialPortHandler接口实现
    bool open(const std::string& portName, int baudRate) override;
    std::string readData(int timeoutMs) override;
    bool writeData(const std::string& data) override;
    void close() override;
    
    bool isOpen() const override;
    std::string getLastError() const override;
    
private:
    bool configureSerialPort();
    std::string readWithTimeout(int timeoutMs);
    
private:
    int portFd_;
    std::string portName_;
    int baudRate_;
    std::atomic<bool> isOpen_;
    std::string lastError_;
};

} // namespace slt

#endif // LINUX_SERIAL_PORT_H