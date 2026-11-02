#pragma once

#include <QHostAddress>
#include <QList>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QString>

#include <algorithm>

namespace BitratePolicy {

enum ReachabilityType
{
    RI_UNKNOWN,
    RI_LAN,
    RI_VPN,
};

enum NetworkType
{
    NT_UNKNOWN,
    NT_ETHERNET,
    NT_WIFI,
};

struct ActiveAddressRoute
{
    ReachabilityType reachability = RI_UNKNOWN;
    NetworkType networkType = NT_UNKNOWN;
};

enum class InterfaceType
{
    Unknown,
    Ethernet,
    Wifi,
    Virtual,
    Ppp,
};

struct InterfaceRoute
{
    QNetworkInterface::InterfaceFlags flags;
    InterfaceType type = InterfaceType::Unknown;
    int mtu = 0;
    QString hardwareAddress;
    QString humanReadableName;
    bool platformVpnType = false;
    QList<QNetworkAddressEntry> addresses;
};

inline ActiveAddressRoute classifyRoute(const QHostAddress& localAddress,
                                       const QHostAddress& peerAddress,
                                       const QList<InterfaceRoute>& interfaces)
{
    if (localAddress.isNull() || peerAddress.isNull()) {
        return {};
    }

    // Follow the socket's chosen source address, not another interface that is up.
    for (const auto& nic : interfaces) {
        if (!(nic.flags & QNetworkInterface::IsUp)) {
            continue;
        }

        for (const auto& address : nic.addresses) {
            if (address.ip() != localAddress) {
                continue;
            }

            NetworkType type = NT_UNKNOWN;
            if (nic.type == InterfaceType::Ethernet) {
                type = NT_ETHERNET;
            }
            else if (nic.type == InterfaceType::Wifi) {
                type = NT_WIFI;
            }

            if (nic.type == InterfaceType::Virtual ||
                    nic.type == InterfaceType::Ppp ||
                    (nic.mtu != 0 && nic.mtu < 1500) ||
                    (nic.flags & QNetworkInterface::IsPointToPoint) ||
                    nic.platformVpnType ||
                    nic.hardwareAddress.startsWith("00:FF", Qt::CaseInsensitive) ||
                    nic.humanReadableName.startsWith("ZeroTier") ||
                    nic.humanReadableName.contains("VPN")) {
                return {RI_VPN, type};
            }

            if (address.prefixLength() >= 0 &&
                    localAddress.isInSubnet(peerAddress, address.prefixLength())) {
                return {RI_LAN, type};
            }

            return {RI_UNKNOWN, type};
        }
    }

    return {};
}

inline int selectBitrate(bool useWifiBitrate,
                         ActiveAddressRoute route,
                         int ethernetBitrateKbps,
                         int wifiBitrateKbps)
{
    if (useWifiBitrate &&
            (route.networkType != NT_ETHERNET || route.reachability == RI_VPN)) {
        return wifiBitrateKbps;
    }

    return ethernetBitrateKbps;
}

inline int clampAutomaticBitrate(int bitrateKbps, bool unlockBitrate)
{
    return std::clamp(bitrateKbps, 500, unlockBitrate ? 500000 : 150000);
}

} // namespace BitratePolicy
