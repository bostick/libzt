/*
 * Copyright (c)2013-2021 ZeroTier, Inc.
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file in the project's root directory.
 *
 * Change Date: 2026-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2.0 of the Apache License.
 */
/****/

/**
 * @file
 *
 * ZeroTier Node Service
 */

#ifndef ZTS_NODE_SERVICE_HPP
#define ZTS_NODE_SERVICE_HPP

#define ZTS_UNUSED_ARG(x) (void)x

#include "Binder.hpp"
#include "Mutex.hpp"
#include "Node.hpp"
#include "Phy.hpp"
#include "PortMapper.hpp"
#include "ZeroTierSockets.h"
#include "version.h"

#include <string>
#include <vector>

#include "b_mutex.h"

#define ZTS_SERVICE_THREAD_NAME        "ZTServiceThread"
#define ZTS_EVENT_CALLBACK_THREAD_NAME "ZTEventCallbackThread"
// Interface metric for ZeroTier taps -- this ensures that if we are on WiFi and
// also bridged via ZeroTier to the same LAN traffic will (if the OS is sane)
// prefer WiFi.
#define ZT_IF_METRIC 5000
// How often to check for new multicast subscriptions on a tap device
#define ZT_TAP_CHECK_MULTICAST_INTERVAL 5000
// How often to check for local interface addresses
#define ZT_LOCAL_INTERFACE_CHECK_INTERVAL 60000

// Attempt to engage TCP fallback after this many ms of no reply to packets sent to global-scope IPs
#define ZT_TCP_FALLBACK_AFTER 30000

// Fake TLS hello for TCP tunnel outgoing connections (TUNNELED mode)
static const char ZT_TCP_TUNNEL_HELLO[9] = { 0x17,
                                             0x03,
                                             0x03,
                                             0x00,
                                             0x04,
                                             (char)ZEROTIER_ONE_VERSION_MAJOR,
                                             (char)ZEROTIER_ONE_VERSION_MINOR,
                                             (char)((ZEROTIER_ONE_VERSION_REVISION >> 8) & 0xff),
                                             (char)(ZEROTIER_ONE_VERSION_REVISION & 0xff) };

#ifdef __WINDOWS__
#include <windows.h>
#endif

namespace ZeroTier {

extern Mutex lwip_state_m;

class NodeService;
struct InetAddress;
class VirtualTap;
class MAC;
class Events;

/**
 * A TCP connection and related state and buffers
 */
struct TcpConnection {
    enum {
        TCP_UNCATEGORIZED_INCOMING,   // uncategorized incoming connection
        TCP_HTTP_INCOMING,
        TCP_HTTP_OUTGOING,
        TCP_TUNNEL_OUTGOING   // TUNNELED mode proxy outbound connection
    } type;

    NodeService* parent;
    PhySocket* sock;
    InetAddress remoteAddr;
    uint64_t lastReceive;

    std::string readq;
    std::string writeq;
    Mutex writeq_m;
};

/**
 * ZeroTier node service
 */
class NodeService {
  public:
    /**
     * Returned by node main if/when it terminates
     */
    enum ReasonForTermination {
        /**
         * Instance is still running
         */
        ONE_STILL_RUNNING = 0,

        /**
         * Normal shutdown
         */
        ONE_NORMAL_TERMINATION = 1,

        /**
         * A serious unrecoverable error has occurred
         */
        ONE_UNRECOVERABLE_ERROR = 2,

        /**
         * Your identity has collided with another
         */
        ONE_IDENTITY_COLLISION = 3
    };

    /**
     * Local settings for each network
     */
    struct NetworkSettings {
        /**
         * Allow this network to configure IP addresses and routes?
         */
        bool allowManaged;

        /**
         * Whitelist of addresses that can be configured by this network.
         * If empty and allowManaged is true, allow all
         * private/pseudoprivate addresses.
         */
        std::vector<InetAddress> allowManagedWhitelist;

        /**
         * Allow configuration of IPs and routes within global (Internet) IP
         * space?
         */
        bool allowGlobal;

        /**
         * Allow overriding of system default routes for "full tunnel"
         * operation?
         */
        bool allowDefault;
    };

    Phy<NodeService*> _phy;
    Node* _node;

    uint64_t _nodeId;
    unsigned int _primaryPort;
    unsigned int _secondaryPort;
    unsigned int _tertiaryPort;

    unsigned int _randomPortRangeStart;
    unsigned int _randomPortRangeEnd;

    volatile unsigned int _udpPortPickerCounter;

    std::map<uint64_t, unsigned int> peerCache;

    // Local configuration and memo-ized information from it
    Hashtable<uint64_t, std::vector<InetAddress> > _v4Hints GUARDED_BY(_localConfig_m);
    Hashtable<uint64_t, std::vector<InetAddress> > _v6Hints GUARDED_BY(_localConfig_m);
    Hashtable<uint64_t, std::vector<InetAddress> > _v4Blacklists GUARDED_BY(_localConfig_m);
    Hashtable<uint64_t, std::vector<InetAddress> > _v6Blacklists GUARDED_BY(_localConfig_m);
    std::vector<InetAddress> _globalV4Blacklist GUARDED_BY(_localConfig_m);
    std::vector<InetAddress> _globalV6Blacklist GUARDED_BY(_localConfig_m);
    std::vector<InetAddress> _allowManagementFrom GUARDED_BY(_localConfig_m);
    std::vector<std::string> _interfacePrefixBlacklist GUARDED_BY(_localConfig_m);
    Mutex _localConfig_m;

    std::vector<InetAddress> explicitBind;

    /*
     * To attempt to handle NAT/gateway craziness we use three local UDP
     * ports:
     *
     * [0] is the normal/default port, usually 9993
     * [1] is a port derived from our ZeroTier address
     * [2] is a port computed from the normal/default for use with
     * uPnP/NAT-PMP mappings
     *
     * [2] exists because on some gateways trying to do regular NAT-t
     * interferes destructively with uPnP port mapping behavior in very
     * weird buggy ways. It's only used if uPnP/NAT-PMP is enabled in this
     * build.
     */
    unsigned int _ports[3] = { 0 };
    Binder _binder;

    // Time we last received a packet from a global address
    uint64_t _lastDirectReceiveFromGlobal;

    InetAddress _fallbackRelayAddress;
    bool _allowTcpRelay;
    bool _forceTcpRelay;
    uint64_t _lastSendToGlobalV4;

    // Active TCP/IP connections
    std::vector<TcpConnection*> _tcpConnections;
    Mutex _tcpConnections_m;
    TcpConnection* _tcpFallbackTunnel;

    // Last potential sleep/wake event
    uint64_t _lastRestart;

    // Deadline for the next background task service function
    volatile int64_t _nextBackgroundTaskDeadline;

    // Configured networks
    struct NetworkState {
        NetworkState() : tap((VirtualTap*)0)
        {
            // Real defaults are in network 'up' code in network event
            // handler
            settings.allowManaged = true;
            settings.allowGlobal = false;
            settings.allowDefault = false;
        }

        VirtualTap* tap;
        ZT_VirtualNetworkConfig config;   // memcpy() of raw config from core
        std::vector<InetAddress> managedIps;
        NetworkSettings settings;
    };
    std::map<uint64_t, NetworkState> _nets GUARDED_BY(_nets_m);

    /** Lock to control access to network configuration data */
    Mutex _nets_m;
    /** Lock to control access to storage data */
    Mutex _store_m;
    /** Lock to control access to service run state */
    Mutex _run_m;
    // Set to false to force service to stop
    volatile bool _run GUARDED_BY(_run_m);
    /** Lock to control access to termination reason */
    Mutex _termReason_m;
    // Termination status information
    ReasonForTermination _termReason GUARDED_BY(_termReason_m);

    std::string _fatalErrorMessage;

    // uPnP/NAT-PMP port mapper if enabled
    bool _allowPortMapping;
#ifdef ZT_USE_MINIUPNPC
    PortMapper* _portMapper;
#endif
    bool _allowSecondaryPort;

    uint8_t _allowNetworkCaching;
    uint8_t _allowPeerCaching;
    uint8_t _allowIdentityCaching;
    uint8_t _allowRootSetCaching;

    char _publicIdStr[ZT_IDENTITY_STRING_BUFFER_LENGTH] GUARDED_BY(_store_m) = { 0 };
    char _secretIdStr[ZT_IDENTITY_STRING_BUFFER_LENGTH] GUARDED_BY(_store_m) = { 0 };

    bool _userDefinedWorld;
    char _rootsData[ZTS_STORE_DATA_LEN] GUARDED_BY(_store_m) = { 0 };
    int _rootsDataLen = 0;

    /** Whether the node has successfully come online */
    bool _nodeIsOnline;

    /** Whether we allow the NodeService to generate events for the user */
    bool _eventsEnabled;

    /** Storage path defined by the user */
    std::string _homePath;

    /** System to ingest events from this class and emit them to the user */
    Events* _events;

    NodeService();
    ~NodeService();

    /** Main service loop */
    ReasonForTermination run() REQUIRES(!_nets_m) REQUIRES(!lwip_state_m) REQUIRES(_localConfig_m); // acquires _run_m inside, so needs !_run_m, writes _run, so needs _run_m, reads _termReason, so needs _termReason_m, acquires so needs !_termReason_m

    ReasonForTermination reasonForTermination() const REQUIRES(!_termReason_m);

    std::string fatalErrorMessage() const REQUIRES(!_termReason_m);

    /** Stop the node and service */
    void terminate() REQUIRES(!_run_m) REQUIRES(_localConfig_m);

    /** Apply or update managed IPs for a configured network */
    void syncManagedStuff(NetworkState& n);

    void phyOnDatagram(
        PhySocket* sock,
        void** uptr,
        const struct sockaddr* localAddr,
        const struct sockaddr* from,
        void* data,
        unsigned long len) REQUIRES(_localConfig_m) REQUIRES(!_run_m) REQUIRES(!_termReason_m);

    void phyOnTcpConnect(PhySocket* sock, void** uptr, bool success);

    int nodeVirtualNetworkConfigFunction(
        uint64_t net_id,
        void** nuptr,
        enum ZT_VirtualNetworkConfigOperation op,
        const ZT_VirtualNetworkConfig* nwc) REQUIRES(!_nets_m);

    void nodeEventCallback(enum ZT_Event event, const void* metaData) REQUIRES(!_run_m) REQUIRES(!_termReason_m) REQUIRES(_localConfig_m);

    zts_net_info_t* prepare_network_details_msg(const NetworkState& n);

    void generateSyntheticEvents() REQUIRES(!_nets_m) REQUIRES(!lwip_state_m);

    void sendEventToUser(unsigned int zt_event_code, const void* obj, unsigned int len = 0);

    /** Join a network */
    int join(uint64_t net_id);

    /** Leave a network */
    int leave(uint64_t net_id);

    /** Return whether the network is ready for transport services */
    bool networkIsReady(uint64_t net_id) const REQUIRES(!_nets_m);

    /** Lock the service so we can perform queries */
    void obtainLock() const REQUIRES(!_nets_m);

    /** Unlock the service */
    void releaseLock() const REQUIRES(_nets_m);

    /** Return number of assigned addresses on the network. Service must be locked. */
    int addressCount(uint64_t net_id) const REQUIRES(_nets_m); // ?

    /** Return number of managed routes on the network. Service must be locked. */
    int routeCount(uint64_t net_id) const REQUIRES(_nets_m); // ?

    /** Return number of multicast subscriptions on the network. Service must be locked. */
    int multicastSubCount(uint64_t net_id) const REQUIRES(_nets_m); // ?

    /** Return number of known physical paths to the peer. Service must be locked. */
    int pathCount(uint64_t peer_id) const;

    int getAddrAtIdx(uint64_t net_id, unsigned int idx, char* dst, unsigned int len) REQUIRES(_nets_m); // ?

    int getRouteAtIdx(
        uint64_t net_id,
        unsigned int idx,
        char* target,
        char* via,
        unsigned int len,
        uint16_t* flags,
        uint16_t* metric) REQUIRES(_nets_m);

    int getMulticastSubAtIdx(uint64_t net_id, unsigned int idx, uint64_t* mac, uint32_t* adi) REQUIRES(_nets_m); // ?

    int getPathAtIdx(uint64_t peer_id, unsigned int idx, char* path, unsigned int len);

    /** Orbit a moon */
    int orbit(uint64_t moonWorldId, uint64_t moonSeed) REQUIRES(!_run_m);

    /** De-orbit a moon */
    int deorbit(uint64_t moonWorldId) REQUIRES(!_run_m);

    /** Return the integer-form of the node's identity */
    uint64_t getNodeId() REQUIRES(!_run_m);

    /** Gets the node's identity */
    int getIdentity(char* keypair, unsigned int* len);

    /** Set the node's identity */
    int setIdentity(const char* keypair, unsigned int len) REQUIRES(!_run_m) REQUIRES(!_store_m);

    void nodeStatePutFunction(enum ZT_StateObjectType type, const uint64_t id[2], const void* data, unsigned int len) REQUIRES(!_store_m);

    int nodeStateGetFunction(enum ZT_StateObjectType type, const uint64_t id[2], void* data, unsigned int maxlen);

    int nodeWirePacketSendFunction(
        const int64_t localSocket,
        const struct sockaddr_storage* addr,
        const void* data,
        unsigned int len,
        unsigned int ttl);

    void nodeVirtualNetworkFrameFunction(
        uint64_t net_id,
        void** nuptr,
        uint64_t sourceMac,
        uint64_t destMac,
        unsigned int etherType,
        unsigned int vlanId,
        const void* data,
        unsigned int len);

    int nodePathCheckFunction(uint64_t ztaddr, const int64_t localSocket, const struct sockaddr_storage* remoteAddr) REQUIRES(!_localConfig_m) REQUIRES(!_nets_m);

    int nodePathLookupFunction(uint64_t ztaddr, unsigned int family, struct sockaddr_storage* result);

    void tapFrameHandler(
        uint64_t net_id,
        const MAC& from,
        const MAC& to,
        unsigned int etherType,
        unsigned int vlanId,
        const void* data,
        unsigned int len);

    int shouldBindInterface(const char* ifname, const InetAddress& ifaddr) REQUIRES(!_localConfig_m) REQUIRES(!_nets_m);

    unsigned int _getRandomPort(unsigned int minPort, unsigned int maxPort);

    int _trialBind(unsigned int port);

    /** Return whether the NodeService is running */
    int isRunning() const REQUIRES(_run_m);

    /** Return whether the node is online */
    int nodeIsOnline() const;

    /** Instruct the NodeService on where to look for identity files and caches */
    int setHomePath(const char* homePath) REQUIRES(!_run_m);

    /** Set the primary port */
    int setPrimaryPort(unsigned short primaryPort) REQUIRES(!_run_m);

    /** Set random range to select backup ports from */
    int setRandomPortRange(unsigned short startPort, unsigned short endPort) REQUIRES(!_run_m);

    /** Get the primary port */
    unsigned short getPrimaryPort() const;

    /** Allow or disallow port-mapping */
    int allowPortMapping(unsigned int allowed) REQUIRES(!_run_m);

    /** Allow or disallow backup port */
    int allowSecondaryPort(unsigned int allowed) REQUIRES(!_run_m);

    /** Set the event system instance used to convey messages to the user */
    int setUserEventSystem(Events* events) REQUIRES(!_run_m);

    /** Set the address and port for the tcp relay that ZeroTier should use */
    void setTcpRelayAddress(const char* tcpRelayAddr, unsigned short tcpRelayPort);

    /** Allow ZeroTier to use the TCP relay */
    void allowTcpRelay(bool enabled);

    /** Force ZeroTier to only use the the TCP relay */
    void forceTcpRelay(bool enabled);

    void enableEvents() REQUIRES(!_run_m);

    /** Set the roots definition */
    int setRoots(const void* data, unsigned int len) REQUIRES(!_run_m) REQUIRES(!_store_m);

    /** Enable or disable low-bandwidth mode (sends less ambient traffic, network updates happen less frequently) */
    int setLowBandwidthMode(bool enabled);

    /** Add Interface prefix to blacklist (prevents ZeroTier from using that interface) */
    int addInterfacePrefixToBlacklist(const char* prefix, unsigned int len) REQUIRES(!_run_m) REQUIRES(!_localConfig_m);

    /** Return the MAC Address of the node in the given network */
    uint64_t getMACAddress(uint64_t net_id) const REQUIRES(!_nets_m) REQUIRES(!_run_m);

    /** Get the string format name of a network */
    int getNetworkName(uint64_t net_id, char* dst, unsigned int len) const REQUIRES(!_nets_m) REQUIRES(!_run_m);

    /** Allow ZeroTier to cache peer hints to storage */
    int allowPeerCaching(unsigned int allowed) REQUIRES(!_run_m);

    /** Allow ZeroTier to cache network info to storage */
    int allowNetworkCaching(unsigned int allowed) REQUIRES(!_run_m);

    /** Allow ZeroTier to write identities to storage */
    int allowIdentityCaching(unsigned int allowed) REQUIRES(!_run_m);

    /** Allow ZeroTier to cache root definitions to storage */
    int allowRootSetCaching(unsigned int allowed) REQUIRES(!_run_m);

    /** Return whether broadcast is enabled on the given network */
    int getNetworkBroadcast(uint64_t net_id) REQUIRES(!_run_m) REQUIRES(!_nets_m);

    /** Return the MTU of the given network */
    int getNetworkMTU(uint64_t net_id) REQUIRES(!_run_m) REQUIRES(!_nets_m);

    /** Return whether the network is public or private */
    int getNetworkType(uint64_t net_id) REQUIRES(!_run_m) REQUIRES(!_nets_m);

    /** Return the status of the network join */
    int getNetworkStatus(uint64_t net_id) REQUIRES(!_run_m) REQUIRES(!_nets_m);

    /** Get the first address assigned by the network */
    int getFirstAssignedAddr(uint64_t net_id, unsigned int family, struct zts_sockaddr_storage* addr) REQUIRES(!_nets_m);

    /** Get an array of assigned addresses for the given network */
    int getAllAssignedAddr(uint64_t net_id, struct zts_sockaddr_storage* addr, unsigned int* count) REQUIRES(!_nets_m);

    /** Return whether a managed route of the given family has been assigned by the network */
    int networkHasRoute(uint64_t net_id, unsigned int family) REQUIRES(!_nets_m);

    /** Return whether an address of the given family has been assigned by the network */
    int addrIsAssigned(uint64_t net_id, unsigned int family) REQUIRES(!_nets_m);

    void phyOnTcpAccept(PhySocket* sockL, PhySocket* sockN, void** uptrL, void** uptrN, const struct sockaddr* from)
    {
        ZTS_UNUSED_ARG(sockL);
        ZTS_UNUSED_ARG(sockN);
        ZTS_UNUSED_ARG(uptrL);
        ZTS_UNUSED_ARG(uptrN);
        ZTS_UNUSED_ARG(from);
    }

    void phyOnTcpClose(PhySocket* sock, void** uptr);

    void phyOnTcpData(PhySocket* sock, void** uptr, void* data, unsigned long len);

    void phyOnTcpWritable(PhySocket* sock, void** uptr);

    void phyOnFileDescriptorActivity(PhySocket* sock, void** uptr, bool readable, bool writable)
    {
        ZTS_UNUSED_ARG(sock);
        ZTS_UNUSED_ARG(uptr);
        ZTS_UNUSED_ARG(readable);
        ZTS_UNUSED_ARG(writable);
    }
    void phyOnUnixAccept(PhySocket* sockL, PhySocket* sockN, void** uptrL, void** uptrN)
    {
        ZTS_UNUSED_ARG(sockL);
        ZTS_UNUSED_ARG(sockN);
        ZTS_UNUSED_ARG(uptrL);
        ZTS_UNUSED_ARG(uptrN);
    }
    void phyOnUnixClose(PhySocket* sock, void** uptr)
    {
        ZTS_UNUSED_ARG(sock);
        ZTS_UNUSED_ARG(uptr);
    }
    void phyOnUnixData(PhySocket* sock, void** uptr, void* data, unsigned long len)
    {
        ZTS_UNUSED_ARG(sock);
        ZTS_UNUSED_ARG(uptr);
        ZTS_UNUSED_ARG(data);
        ZTS_UNUSED_ARG(len);
    }

    void phyOnUnixWritable(PhySocket* sock, void** uptr)
    {
        ZTS_UNUSED_ARG(sock);
        ZTS_UNUSED_ARG(uptr);
    }
};

}   // namespace ZeroTier

#endif
