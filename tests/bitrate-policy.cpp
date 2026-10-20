#include "../app/backend/bitratepolicy.h"

#include <QCoreApplication>

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace BitratePolicy;

static int checks = 0;

static void check(bool condition, const char* message)
{
    ++checks;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

static InterfaceRoute makeInterface(InterfaceType type, const char* ip,
                                int prefix = 24, int mtu = 1500)
{
    InterfaceRoute nic;
    nic.flags = QNetworkInterface::IsUp | QNetworkInterface::IsRunning;
    nic.type = type;
    nic.mtu = mtu;
    QNetworkAddressEntry entry;
    entry.setIp(QHostAddress(QString::fromLatin1(ip)));
    entry.setPrefixLength(prefix);
    nic.addresses.append(entry);
    return nic;
}

static void checkRoute(ActiveAddressRoute actual, ReachabilityType reachability,
                       NetworkType network, const char* message)
{
    check(actual.reachability == reachability && actual.networkType == network, message);
}

static void routeSelectionTests()
{
    auto wifi = makeInterface(InterfaceType::Wifi, "192.0.2.2");
    auto ethernet = makeInterface(InterfaceType::Ethernet, "198.51.100.2");
    auto route = classifyRoute(QHostAddress("198.51.100.2"),
                               QHostAddress("198.51.100.8"), {wifi, ethernet});
    checkRoute(route, RI_LAN, NT_ETHERNET, "Wi-Fi being up must not override the selected Ethernet route");
    check(selectBitrate(true, route, 175500, 63500) == 175500, "Selected Ethernet must use the exact manual primary bitrate");

    route = classifyRoute(QHostAddress("192.0.2.2"),
                           QHostAddress("192.0.2.8"), {ethernet, wifi});
    checkRoute(route, RI_LAN, NT_WIFI, "An idle Ethernet interface must not override a Wi-Fi route");
    check(selectBitrate(true, route, 175500, 63500) == 63500, "Selected Wi-Fi must use the exact manual Wi-Fi bitrate");

    checkRoute(classifyRoute(QHostAddress("192.0.2.2"), QHostAddress("192.0.2.8"), {wifi}),
                RI_LAN, NT_WIFI, "Unplugging Ethernet must select the new Wi-Fi route without cached state");
    checkRoute(classifyRoute(QHostAddress("198.51.100.2"), QHostAddress("198.51.100.8"), {ethernet}),
                RI_LAN, NT_ETHERNET, "Replugging Ethernet must select the new Ethernet route without cached state");

    ethernet.flags = QNetworkInterface::IsRunning;
    checkRoute(classifyRoute(QHostAddress("198.51.100.2"), QHostAddress("198.51.100.8"), {ethernet, wifi}),
                RI_UNKNOWN, NT_UNKNOWN, "A down interface must not confirm Ethernet");
    checkRoute(classifyRoute(QHostAddress("203.0.113.2"), QHostAddress("203.0.113.8"), {wifi}),
                RI_UNKNOWN, NT_UNKNOWN, "An unmatched socket source must retain the safe unknown route");
    checkRoute(classifyRoute(QHostAddress(), QHostAddress("192.0.2.8"), {wifi}),
                RI_UNKNOWN, NT_UNKNOWN, "A missing socket source must not classify an interface");
    checkRoute(classifyRoute(QHostAddress("192.0.2.2"), QHostAddress(), {wifi}),
                RI_UNKNOWN, NT_UNKNOWN, "A missing socket peer must not classify an interface");

    ethernet = makeInterface(InterfaceType::Ethernet, "198.51.100.2");
    checkRoute(classifyRoute(QHostAddress("198.51.100.2"), QHostAddress("203.0.113.8"), {ethernet}),
                RI_UNKNOWN, NT_ETHERNET, "An off-subnet destination must not erase confirmed physical Ethernet");
    auto ipv6 = makeInterface(InterfaceType::Ethernet, "2001:db8:1::2", 64);
    checkRoute(classifyRoute(QHostAddress("2001:db8:1::2"), QHostAddress("2001:db8:1::8"), {ipv6}),
                RI_LAN, NT_ETHERNET, "IPv6 routes must match the socket source and subnet");
    auto invalidPrefix = makeInterface(InterfaceType::Ethernet, "198.51.100.2", -1);
    checkRoute(classifyRoute(QHostAddress("198.51.100.2"), QHostAddress("198.51.100.8"), {invalidPrefix}),
                RI_UNKNOWN, NT_ETHERNET, "Missing prefix metadata must fall back without an assertion");
}

static void vpnClassificationTests()
{
    auto classify = [](const InterfaceRoute& nic) {
        return classifyRoute(QHostAddress("192.0.2.2"), QHostAddress("192.0.2.8"), {nic});
    };
    for (auto type : {InterfaceType::Virtual, InterfaceType::Ppp}) {
        auto route = classify(makeInterface(type, "192.0.2.2"));
        checkRoute(route, RI_VPN, NT_UNKNOWN, "Virtual and PPP links must be treated as VPN candidates");
        check(selectBitrate(true, route, 175500, 63500) == 63500, "VPN link types must use the fallback preset");
    }

    auto nic = makeInterface(InterfaceType::Ethernet, "192.0.2.2", 24, 1499);
    checkRoute(classify(nic), RI_VPN, NT_ETHERNET, "A reduced-MTU Ethernet link must use the existing VPN heuristic");
    nic.mtu = 1500;
    checkRoute(classify(nic), RI_LAN, NT_ETHERNET, "A normal Ethernet MTU must not be classified as a VPN");
    nic.mtu = 0;
    checkRoute(classify(nic), RI_LAN, NT_ETHERNET, "An unavailable MTU must not be classified as a reduced MTU");
    nic.flags |= QNetworkInterface::IsPointToPoint;
    checkRoute(classify(nic), RI_VPN, NT_ETHERNET, "Point-to-point links must retain the safe VPN preset");
    nic = makeInterface(InterfaceType::Ethernet, "192.0.2.2");
    nic.platformVpnType = true;
    checkRoute(classify(nic), RI_VPN, NT_ETHERNET, "Platform VPN interface types must override physical Ethernet");
    nic.platformVpnType = false;
    nic.hardwareAddress = "00:ff:12:34:56:78";
    checkRoute(classify(nic), RI_VPN, NT_ETHERNET, "The OpenVPN TAP MAC heuristic must be case insensitive");
    nic.hardwareAddress.clear();
    nic.humanReadableName = "ZeroTier network";
    checkRoute(classify(nic), RI_VPN, NT_ETHERNET, "ZeroTier interfaces must use the VPN preset");
    nic.humanReadableName = "Work VPN";
    checkRoute(classify(nic), RI_VPN, NT_ETHERNET, "Named VPN interfaces must use the VPN preset");
}

static void bitrateSelectionTests()
{
    const struct {
        ActiveAddressRoute route;
        int expected;
    } cases[] = {
        {{RI_LAN, NT_ETHERNET}, 175500},
        {{RI_UNKNOWN, NT_ETHERNET}, 175500},
        {{RI_VPN, NT_ETHERNET}, 63500},
        {{RI_LAN, NT_WIFI}, 63500},
        {{RI_VPN, NT_WIFI}, 63500},
        {{RI_UNKNOWN, NT_WIFI}, 63500},
        {{RI_LAN, NT_UNKNOWN}, 63500},
        {{RI_VPN, NT_UNKNOWN}, 63500},
        {{RI_UNKNOWN, NT_UNKNOWN}, 63500},
    };
    for (auto test : cases) {
        check(selectBitrate(false, test.route, 175500, 63500) == 175500,
              "Disabling the toggle must always preserve the primary preset");
        check(selectBitrate(true, test.route, 175500, 63500) == test.expected,
              "Only confirmed non-VPN Ethernet may select the primary preset");
    }
    check(selectBitrate(true, {RI_LAN, NT_WIFI}, 150000, 80000) == 80000,
          "A manual Wi-Fi preset equal to a codec default must not be normalized");
    check(selectBitrate(true, {RI_LAN, NT_WIFI}, 500, 500000) == 500000,
          "The selector must preserve explicit presets instead of imposing a default or cap");
}

static void automaticBitrateTests()
{
    check(clampAutomaticBitrate(248000, false) == 150000,
          "The automatic 4K/144 FPS/4:4:4 default must respect the locked bitrate limit");
    check(clampAutomaticBitrate(248000, true) == 248000,
          "Unlocking must permit the automatic 4K/144 FPS/4:4:4 default");
    for (bool unlocked : {false, true}) {
        check(clampAutomaticBitrate(35000, unlocked) == 35000,
              "An automatic bitrate within range must remain unchanged");
        check(clampAutomaticBitrate(500, unlocked) == 500,
              "The automatic lower bound must be inclusive");
        check(clampAutomaticBitrate(0, unlocked) == 500,
              "An undersized automatic default must retain the minimum valid bitrate");
        check(clampAutomaticBitrate(std::numeric_limits<int>::min(), unlocked) == 500,
              "A negative extreme must safely clamp without arithmetic overflow");
    }
    check(clampAutomaticBitrate(150000, false) == 150000,
          "The locked upper bound must be inclusive");
    check(clampAutomaticBitrate(500000, true) == 500000,
          "The unlocked upper bound must be inclusive");
    check(clampAutomaticBitrate(std::numeric_limits<int>::max(), false) == 150000,
          "A large automatic default must safely respect the locked ceiling");
    check(clampAutomaticBitrate(std::numeric_limits<int>::max(), true) == 500000,
          "A large automatic default must safely respect the unlocked ceiling");
    check(selectBitrate(false, {RI_UNKNOWN, NT_UNKNOWN}, 248000, 20000) == 248000,
          "An explicit fixed primary preset must not pass through automatic clamping");
    check(selectBitrate(true, {RI_LAN, NT_WIFI}, 150000, 248000) == 248000,
          "An explicit Wi-Fi preset must not pass through automatic clamping");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        routeSelectionTests();
        vpnClassificationTests();
        bitrateSelectionTests();
        automaticBitrateTests();
    }
    catch (const std::exception& error) {
        std::cerr << "Bitrate policy regression: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " bitrate route/preset checks passed\n";
    return 0;
}
