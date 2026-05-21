// TCP服务端通信处理器实现
// 文件名: tcp_server_handler.cpp
// 描述: 通过 TCP 监听端口接收 SLT 命令，返回结果，协议格式与串口模式一致

#include "tcp_server_handler.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>

namespace slt {

TcpServerHandler::TcpServerHandler()
    : listenFd_(-1)
    , clientFd_(-1)
    , bindPort_(9999)
    , isOpen_(false) {
}

TcpServerHandler::~TcpServerHandler() {
    close();
}

bool TcpServerHandler::open(const std::string& portName, int baudRate) {
    if (isOpen_) {
        close();
    }

    // 解析地址: "0.0.0.0:9999" 或 "9999"
    std::string addr = "0.0.0.0";
    int port = 9999;

    size_t colonPos = portName.rfind(':');
    if (colonPos != std::string::npos) {
        addr = portName.substr(0, colonPos);
        try {
            port = std::stoi(portName.substr(colonPos + 1));
        } catch (...) {
            lastError_ = "Invalid TCP port in: " + portName;
            return false;
        }
    } else {
        // 纯数字视为端口号
        try {
            port = std::stoi(portName);
        } catch (...) {
            lastError_ = "Invalid TCP address: " + portName;
            return false;
        }
    }

    bindAddr_ = addr;
    bindPort_ = port;

    // 创建监听 socket
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        lastError_ = "Failed to create socket: " + std::string(strerror(errno));
        return false;
    }

    // 允许地址复用
    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 绑定
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, addr.c_str(), &serverAddr.sin_addr);

    if (::bind(listenFd_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        lastError_ = "Failed to bind " + addr + ":" + std::to_string(port) + ": " + strerror(errno);
        closeListen();
        return false;
    }

    // 监听
    if (::listen(listenFd_, 1) < 0) {
        lastError_ = "Failed to listen: " + std::string(strerror(errno));
        closeListen();
        return false;
    }

    // 设置非阻塞，用于 accept 超时控制
    int flags = fcntl(listenFd_, F_GETFL, 0);
    fcntl(listenFd_, F_SETFL, flags | O_NONBLOCK);

    isOpen_ = true;
    lastError_.clear();

    std::cout << "[INFO] TCP server listening on " << addr << ":" << port << std::endl;
    return true;
}

std::string TcpServerHandler::readData(int timeoutMs) {
    if (!isOpen_) {
        lastError_ = "TCP server is not open";
        return "";
    }

    // 如果没有客户端连接，先等待连接
    if (clientFd_ < 0) {
        if (!acceptConnection(timeoutMs)) {
            return "";
        }
    }

    // 从客户端读取数据
    std::string result;
    char buffer[4096];
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed >= timeoutMs) {
            break;
        }

        int bytesAvailable = 0;
        if (ioctl(clientFd_, FIONREAD, &bytesAvailable) < 0) {
            // 客户端可能已断开
            closeClient();
            lastError_ = "Client disconnected";
            return "";
        }

        if (bytesAvailable > 0) {
            int bytesToRead = std::min(bytesAvailable, (int)sizeof(buffer));
            ssize_t bytesRead = ::read(clientFd_, buffer, bytesToRead);
            if (bytesRead > 0) {
                result.append(buffer, bytesRead);
            } else if (bytesRead <= 0) {
                // 客户端断开
                closeClient();
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // 收到结束标记，提前返回
        if (result.find("^^") != std::string::npos) {
            break;
        }
    }

    return result;
}

bool TcpServerHandler::writeData(const std::string& data) {
    if (!isOpen_ || clientFd_ < 0) {
        lastError_ = "No client connected";
        return false;
    }

    ssize_t bytesWritten = ::write(clientFd_, data.c_str(), data.length());
    if (bytesWritten < 0) {
        lastError_ = "Failed to write to client: " + std::string(strerror(errno));
        closeClient();
        return false;
    }

    if (bytesWritten != static_cast<ssize_t>(data.length())) {
        lastError_ = "Incomplete write to client";
        closeClient();
        return false;
    }

    // 一问一答模式：发送完响应后关闭客户端连接，让 nc 能正常退出
    closeClient();
    return true;
}

void TcpServerHandler::close() {
    closeClient();
    closeListen();
    isOpen_ = false;
}

bool TcpServerHandler::isOpen() const {
    return isOpen_;
}

std::string TcpServerHandler::getLastError() const {
    return lastError_;
}

// ================== 私有方法 ==================

bool TcpServerHandler::acceptConnection(int timeoutMs) {
    if (listenFd_ < 0) {
        return false;
    }

    std::cout << "[INFO] Waiting for TCP client connection..." << std::endl;

    auto startTime = std::chrono::steady_clock::now();
    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int fd = ::accept(listenFd_, (struct sockaddr*)&clientAddr, &clientLen);
        if (fd >= 0) {
            clientFd_ = fd;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            std::cout << "[INFO] Client connected from " << ip << ":"
                      << ntohs(clientAddr.sin_port) << std::endl;
            return true;
        }

        // 非阻塞 accept，检查超时
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed >= timeoutMs) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return false;
}

void TcpServerHandler::closeClient() {
    if (clientFd_ >= 0) {
        ::close(clientFd_);
        clientFd_ = -1;
    }
}

void TcpServerHandler::closeListen() {
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
}

} // namespace slt
