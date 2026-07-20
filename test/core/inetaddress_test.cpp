#include <gtest/gtest.h>

#include <arpa/inet.h>

#include "inetaddress.h"


namespace
{

    sockaddr_in makeSockAddr(const char *ip, uint16_t port)
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        EXPECT_EQ(::inet_pton(AF_INET, ip, &addr.sin_addr), 1);


        return addr;
    }

} // namespace


TEST(InetAddressTest, DefaultConstructorUsesLoopbackAndZeroPort)
{
    InetAddress address;

    ASSERT_NE(address.getSockAddr(), nullptr);
    EXPECT_EQ(address.getSockAddr()->sin_family, AF_INET);
    EXPECT_EQ(address.toIp(), "127.0.0.1");
    EXPECT_EQ(address.toPort(), 0);
    EXPECT_EQ(address.toIpPort(), "127.0.0.1:0");
}

TEST(InetAddressTest, ConstructorStoresIpAndPort)
{
    InetAddress address(2007, "192.168.1.10");

    ASSERT_NE(address.getSockAddr(), nullptr);
    EXPECT_EQ(address.getSockAddr()->sin_family, AF_INET);
    EXPECT_EQ(address.toIp(), "192.168.1.10");
    EXPECT_EQ(address.toPort(), 2007);
    EXPECT_EQ(address.toIpPort(), "192.168.1.10:2007");
    EXPECT_EQ(::ntohs(address.getSockAddr()->sin_port), 2007);
}

TEST(InetAddressTest, ConstructorFromSockAddrPreservesAddress)
{
    const sockaddr_in addr = makeSockAddr("10.0.0.5", 65535);

    InetAddress address(addr);

    EXPECT_EQ(address.toIp(), "10.0.0.5");
    EXPECT_EQ(address.toPort(), 65535);
    EXPECT_EQ(address.toIpPort(), "10.0.0.5:65535");
}

TEST(InetAddressTest, SetSockAddrReplacesStoredAddress)
{
    InetAddress address(80, "8.8.8.8");
    const sockaddr_in addr = makeSockAddr("172.16.0.3", 9000);

    address.setSockAddr(addr);

    EXPECT_EQ(address.toIp(), "172.16.0.3");
    EXPECT_EQ(address.toPort(), 9000);
    EXPECT_EQ(address.toIpPort(), "172.16.0.3:9000");
}
