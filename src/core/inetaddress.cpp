#include "inetaddress.h"

#include <cstring>


InetAddress::InetAddress(uint16_t port, const std::string &ip)
{
    std::memset(&m_addr, 0, sizeof(m_addr));

    m_addr.sin_family = AF_INET;
    // 本地字节序转为网络字节序。
    m_addr.sin_port = ::htons(port);
    m_addr.sin_addr.s_addr = ::inet_addr(ip.c_str());
}

std::string InetAddress::toIp() const
{
    // ip
    char ip[64] = {0};
    ::inet_ntop(AF_INET, &m_addr.sin_addr, ip, sizeof(ip));
    return ip;
}

std::string InetAddress::toIpPort() const
{
    // ip:port
    char ip[64] = {0};
    ::inet_ntop(AF_INET, &m_addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(::ntohs(m_addr.sin_port));
}
