// Linux串口处理器实现
// 文件名: linux_serial_port.cpp

#include "linux_serial_port.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <thread>
#include <iostream>

namespace slt {

LinuxSerialPort::LinuxSerialPort() 
    : portFd_(-1)
    , baudRate_(115200)
    , isOpen_(false) {
}

LinuxSerialPort::~LinuxSerialPort() {
    close();
}

bool LinuxSerialPort::open(const std::string& portName, int baudRate) {
    if (isOpen_) {
        close();
    }
    
    portFd_ = ::open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (portFd_ < 0) {
        lastError_ = "Failed to open serial port: " + std::string(strerror(errno));
        return false;
    }
    
    baudRate_ = baudRate;
    portName_ = portName;
    
    if (!configureSerialPort()) {
        close();
        return false;
    }
    
    isOpen_ = true;
    lastError_.clear();
    return true;
}

std::string LinuxSerialPort::readData(int timeoutMs) {
    if (!isOpen_) {
        lastError_ = "Serial port is not open";
        return "";
    }
    
    return readWithTimeout(timeoutMs);
}

bool LinuxSerialPort::writeData(const std::string& data) {
    if (!isOpen_) {
        lastError_ = "Serial port is not open";
        return false;
    }
    
    ssize_t bytesWritten = ::write(portFd_, data.c_str(), data.length());
    if (bytesWritten < 0) {
        lastError_ = "Failed to write to serial port: " + std::string(strerror(errno));
        return false;
    }
    
    if (bytesWritten != static_cast<ssize_t>(data.length())) {
        lastError_ = "Incomplete write to serial port";
        return false;
    }
    
    // 确保数据被发送
    tcdrain(portFd_);
    return true;
}

void LinuxSerialPort::close() {
    if (portFd_ >= 0) {
        ::close(portFd_);
        portFd_ = -1;
    }
    
    isOpen_ = false;
    portName_.clear();
    baudRate_ = 0;
    lastError_.clear();
}

bool LinuxSerialPort::isOpen() const {
    return isOpen_;
}

std::string LinuxSerialPort::getLastError() const {
    return lastError_;
}

// ================== 私有方法 ==================
bool LinuxSerialPort::configureSerialPort() {
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    
    if (tcgetattr(portFd_, &tty) != 0) {
        lastError_ = "Failed to get serial port attributes: " + std::string(strerror(errno));
        return false;
    }
    
    // 设置波特率
    speed_t speed;
    switch (baudRate_) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
        default: speed = B115200; break;
    }
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    
    // 8N1配置
    tty.c_cflag &= ~PARENB;    // 无奇偶校验
    tty.c_cflag &= ~CSTOPB;    // 1位停止位
    tty.c_cflag &= ~CSIZE;     // 清除数据位设置
    tty.c_cflag |= CS8;        // 8位数据位
    tty.c_cflag &= ~CRTSCTS;   // 无硬件流控
    tty.c_cflag |= CREAD | CLOCAL; // 启用接收，忽略调制解调器控制线
    
    // 原始模式
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    
    // 超时设置
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1; // 0.1秒超时
    
    if (tcsetattr(portFd_, TCSANOW, &tty) != 0) {
        lastError_ = "Failed to set serial port attributes: " + std::string(strerror(errno));
        return false;
    }
    
    return true;
}

std::string LinuxSerialPort::readWithTimeout(int timeoutMs) {
    std::string result;
    char buffer[256];
    
    auto startTime = std::chrono::steady_clock::now();
    
    while (true) {
        // 检查超时
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - startTime).count();
        
        if (elapsed >= timeoutMs) {
            break;
        }
        
        // 检查是否有数据可读
        int bytesAvailable = 0;
        if (ioctl(portFd_, FIONREAD, &bytesAvailable) < 0) {
            lastError_ = "Failed to check available bytes: " + std::string(strerror(errno));
            break;
        }
        
        if (bytesAvailable > 0) {
            int bytesToRead = std::min(bytesAvailable, (int)sizeof(buffer));
            ssize_t bytesRead = ::read(portFd_, buffer, bytesToRead);
            if (bytesRead > 0) {
                result.append(buffer, bytesRead);
            } else if (bytesRead < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    lastError_ = "Failed to read from serial port: " + std::string(strerror(errno));
                    break;
                }
            }
        } else {
            // 没有数据，短暂休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 如果收到结束标记，提前返回
        if (result.find("^^") != std::string::npos) {
            break;
        }
    }
    
    return result;
}

} // namespace slt