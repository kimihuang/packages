// 测试用模拟串口实现
// 文件名: test_mock_serial_port.cpp

#include "test_mock_serial_port.h"
#include <chrono>
#include <thread>

namespace slt {
namespace test {

MockSerialPort::MockSerialPort()
    : isOpen_(false) {
}

MockSerialPort::~MockSerialPort() {
    close();
}

bool MockSerialPort::open(const std::string& portName, int baudRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (isOpen_) {
        close();
    }
    
    isOpen_ = true;
    lastError_.clear();
    return true;
}

std::string MockSerialPort::readData(int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (!isOpen_) {
        lastError_ = "Serial port is not open";
        return "";
    }
    
    // 等待数据或超时
    if (dataQueue_.empty()) {
        if (timeoutMs > 0) {
            condVar_.wait_for(lock, std::chrono::milliseconds(timeoutMs));
        } else {
            condVar_.wait(lock);
        }
    }
    
    if (dataQueue_.empty()) {
        return ""; // 超时返回空
    }
    
    std::string data = dataQueue_.front();
    dataQueue_.pop();
    
    return data;
}

bool MockSerialPort::writeData(const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!isOpen_) {
        lastError_ = "Serial port is not open";
        return false;
    }
    
    lastWrittenData_ = data;
    return true;
}

void MockSerialPort::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    isOpen_ = false;
    lastError_.clear();
}

bool MockSerialPort::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isOpen_;
}

std::string MockSerialPort::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void MockSerialPort::pushMockData(const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    dataQueue_.push(data);
    condVar_.notify_one();
}

std::string MockSerialPort::getLastWrittenData() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastWrittenData_;
}

void MockSerialPort::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<std::string> emptyQueue;
    std::swap(dataQueue_, emptyQueue);
    lastWrittenData_.clear();
    lastError_.clear();
}

} // namespace test
} // namespace slt