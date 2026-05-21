// SLT配置类实现
// 文件名: slt_config.cpp

#include "slt_interfaces.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

namespace slt {

SLTConfig::SLTConfig()
    : serialPort_("/dev/ttyUSB0")
    , baudRate_(115200)
    , logDirectory_("/var/log/slt/")
    , defaultTimeout_(5000)
    , maxCommandSize_(4096)
    , tcpAddr_("0.0.0.0")
    , tcpPort_(9999)
    , tcpMode_(false) {
}

bool SLTConfig::loadFromFile(const std::string& configPath) {
    try {
        std::ifstream fin(configPath);
        if (!fin.is_open()) {
            std::cerr << "[ERROR] Failed to open config file: " << configPath << std::endl;
            return false;
        }

        std::string line;
        std::string currentSection;

        while (std::getline(fin, line)) {
            // 去除首尾空白
            size_t start = line.find_first_not_of(" \t\r\n");
            size_t end = line.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start, end - start + 1);

            // 跳过注释和空行
            if (line.empty() || line[0] == '#') continue;

            // 段标记 [section]
            if (line[0] == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            // key: value 或 key=value
            size_t delimPos = line.find(':');
            if (delimPos == std::string::npos)
                delimPos = line.find('=');
            if (delimPos == std::string::npos) continue;

            std::string key = line.substr(0, delimPos);
            std::string value = line.substr(delimPos + 1);
            // 去除value前后空白和引号
            start = value.find_first_not_of(" \t\"'");
            end = value.find_last_not_of(" \t\"'");
            if (start != std::string::npos)
                value = value.substr(start, end - start + 1);

            if (currentSection == "serial" || currentSection.empty()) {
                if (key == "port")          serialPort_ = value;
                else if (key == "baud_rate" || key == "baudRate")
                    baudRate_ = std::stoi(value);
            }
            if (currentSection == "logging") {
                if (key == "directory")     logDirectory_ = value;
            }
            if (currentSection == "commands") {
                if (key == "default_timeout_ms" || key == "defaultTimeout")
                    defaultTimeout_ = std::stoi(value);
                else if (key == "max_command_size" || key == "maxCommandSize")
                    maxCommandSize_ = std::stoi(value);
            }
            if (currentSection == "network") {
                if (key == "mode" || key == "type") {
                    tcpMode_ = (value == "tcp" || value == "TCP" || value == "1" || value == "true");
                } else if (key == "addr" || key == "address" || key == "host") {
                    tcpAddr_ = value;
                } else if (key == "port") {
                    tcpPort_ = std::stoi(value);
                    tcpMode_ = true;  // 配置了 network port 即启用 TCP 模式
                }
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to load config file: " << e.what() << std::endl;
        return false;
    }
}

bool SLTConfig::saveToFile(const std::string& configPath) {
    try {
        std::ofstream fout(configPath);
        if (!fout.is_open()) {
            std::cerr << "[ERROR] Failed to open file for writing: " << configPath << std::endl;
            return false;
        }

        fout << "# SLT Configuration\n";
        fout << "\n[serial]\n";
        fout << "port: " << serialPort_ << "\n";
        fout << "baud_rate: " << baudRate_ << "\n";
        fout << "\n[logging]\n";
        fout << "directory: " << logDirectory_ << "\n";
        fout << "\n[commands]\n";
        fout << "default_timeout_ms: " << defaultTimeout_ << "\n";
        fout << "max_command_size: " << maxCommandSize_ << "\n";
        fout << "\n[network]\n";
        fout << "mode: " << (tcpMode_ ? "tcp" : "serial") << "\n";
        fout << "addr: " << tcpAddr_ << "\n";
        fout << "port: " << tcpPort_ << "\n";

        return fout.good();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to save config file: " << e.what() << std::endl;
        return false;
    }
}

} // namespace slt