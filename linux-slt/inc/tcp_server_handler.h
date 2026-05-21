// TCP服务端通信处理器头文件
// 文件名: tcp_server_handler.h
// 描述: 实现 SerialPortHandler 接口，通过 TCP 接收上位机命令并返回结果
//       命令格式与串口模式完全一致: $$<命令>^^ / $&<结果>^^

#ifndef TCP_SERVER_HANDLER_H
#define TCP_SERVER_HANDLER_H

#include "slt_interfaces.h"
#include <string>
#include <atomic>

namespace slt {

class TcpServerHandler : public SerialPortHandler {
public:
    TcpServerHandler();
    ~TcpServerHandler() override;

    // SerialPortHandler 接口实现
    // portName 格式: "0.0.0.0:9999" 或 "9999" (默认绑定 0.0.0.0)
    bool open(const std::string& portName, int baudRate) override;
    std::string readData(int timeoutMs) override;
    bool writeData(const std::string& data) override;
    void close() override;

    bool isOpen() const override;
    std::string getLastError() const override;

private:
    bool acceptConnection(int timeoutMs);
    void closeClient();
    void closeListen();

    int listenFd_;        // 监听 socket
    int clientFd_;        // 当前客户端 socket
    std::string bindAddr_; // 绑定地址
    int bindPort_;        // 绑定端口
    std::atomic<bool> isOpen_;
    std::string lastError_;
};

} // namespace slt

#endif // TCP_SERVER_HANDLER_H
