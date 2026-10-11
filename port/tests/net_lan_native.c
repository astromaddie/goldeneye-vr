/* Native LAN tests include the production beacon reader and link the production ENet implementation. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "net/net_core.h"
#include "net/netbuf.h"
#include "external/enet.h"
#include "../src/net/net_discovery.c"

int netGetConnectedPlayerCount(void) { return 1; }
int netGetMaxPlayers(void) { return GEVR_MAX_PLAYERS; }
uint8_t netGetLobbyStage(void) { return 0; }
uint8_t netGetLobbyWeaponSet(void) { return 0; }
NetPhase netGetPhase(void) { return NET_PHASE_WAITING; }
void sysLogPrintf(s32 level, const char *format, ...) { (void)level; (void)format; }

static int send_beacon(uint16_t version, const char *name, uint16_t port,
                       uint8_t players, uint8_t max_players, uint8_t phase,
                       uint8_t joinable, uint32_t host_id, int short_packet) {
    int sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender < 0) return -1;
    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(GEVR_DISCOVERY_PORT);
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    NetDiscoveryBeacon beacon;
    memset(&beacon, 0, sizeof(beacon));
    beacon.magic = PD_LE32(GEVR_NET_MAGIC);
    beacon.version = PD_LE16(version);
    beacon.port = PD_LE16(port);
    if (name) {
        size_t length = strlen(name);
        if (length >= sizeof(beacon.name)) length = sizeof(beacon.name);
        memcpy(beacon.name, name, length);
    } else {
        memset(beacon.name, 'A', sizeof(beacon.name));
    }
    beacon.player_count = players;
    beacon.max_players = max_players;
    beacon.phase = phase;
    beacon.joinable = joinable;
    beacon.host_id = PD_LE32(host_id);

    const size_t size = sizeof(beacon) - (short_packet ? 1 : 0);
    const int sent = (int)sendto(sender, (const char *)&beacon, size, 0,
                                 (struct sockaddr *)&destination, sizeof(destination));
    close(sender);
    return sent == (int)size ? 0 : -1;
}

int test_discovery_udp(void) {
    netDiscoveryShutdown();
    if (!netDiscoveryInit()) return 1;
    s_self_id = 0xdeadbeef;
    if (netDiscoveryGetServerCount() != 0) return 2;

    if (send_beacon(GEVR_NET_VERSION - 1, "Old protocol", GEVR_DEFAULT_PORT, 1, 4, 0, 1, 1, 0) ||
        send_beacon(GEVR_NET_VERSION, "Short", GEVR_DEFAULT_PORT, 1, 4, 0, 1, 2, 1) ||
        send_beacon(GEVR_NET_VERSION, NULL, GEVR_DEFAULT_PORT, 1, 4, 0, 1, 3, 0) ||
        send_beacon(GEVR_NET_VERSION, "No port", 0, 1, 4, 0, 1, 4, 0) ||
        send_beacon(GEVR_NET_VERSION, "Bad counts", GEVR_DEFAULT_PORT, 5, 4, 0, 1, 5, 0) ||
        send_beacon(GEVR_NET_VERSION, "Bad phase", GEVR_DEFAULT_PORT, 1, 4, 3, 1, 6, 0) ||
        send_beacon(GEVR_NET_VERSION, "Bad joinable", GEVR_DEFAULT_PORT, 1, 4, 0, 2, 7, 0)) return 3;
    const struct timespec udp_wait = {.tv_sec = 0, .tv_nsec = 10000000};
    nanosleep(&udp_wait, NULL);
    netDiscoveryUpdate(1000);
    if (netDiscoveryGetServerCount() != 0) return 4;

    if (send_beacon(GEVR_NET_VERSION, "Host One", GEVR_DEFAULT_PORT, 1, 4, 0, 1, 8, 0)) return 5;
    nanosleep(&udp_wait, NULL);
    netDiscoveryUpdate(1500);
    if (netDiscoveryGetServerCount() != 1) return 6;
    const NetDiscoveredServer *server = netDiscoveryGetServer(0);
    if (!server || strcmp(server->host_ip, "127.0.0.1") || server->port != GEVR_DEFAULT_PORT ||
        strcmp(server->server_name, "Host One") || server->player_count != 1 || server->max_players != 4 ||
        server->last_seen_ms != 1500) return 7;

    if (send_beacon(GEVR_NET_VERSION, "Host Updated", GEVR_DEFAULT_PORT, 2, 4, 1, 1, 9, 0)) return 8;
    nanosleep(&udp_wait, NULL);
    netDiscoveryUpdate(2000);
    server = netDiscoveryGetServer(0);
    if (netDiscoveryGetServerCount() != 1) return 9;
    if (!server) return 15;
    if (strcmp(server->server_name, "Host Updated")) return 16;
    if (server->player_count != 2) return 17;
    if (server->phase != 1) return 18;
    netDiscoveryUpdate(7001);
    if (netDiscoveryGetServerCount() != 0) return 10;
    netDiscoveryShutdown();

    /* Failed bind must close the socket so a later retry can recover. */
    int blocker = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (blocker < 0) return 11;
    struct sockaddr_in occupied;
    memset(&occupied, 0, sizeof(occupied));
    occupied.sin_family = AF_INET;
    occupied.sin_port = htons(GEVR_DISCOVERY_PORT);
    occupied.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(blocker, (struct sockaddr *)&occupied, sizeof(occupied)) != 0) {
        close(blocker);
        return 12;
    }
    if (netDiscoveryInit() || s_disc_socket != INVALID_SOCKET) {
        netDiscoveryShutdown();
        close(blocker);
        return 13;
    }
    close(blocker);
    if (!netDiscoveryInit()) return 14;
    netDiscoveryShutdown();
    return 0;
}

static int write_header(uint8_t *data, size_t capacity, uint32_t sequence, size_t *size) {
    struct netbuf buffer = {.data = data, .size = (u32)capacity};
    netbufStartWrite(&buffer);
    netbufWriteU32(&buffer, GEVR_NET_MAGIC);
    netbufWriteU16(&buffer, GEVR_NET_VERSION);
    netbufWriteU8(&buffer, NET_MSG_NONE);
    netbufWriteU8(&buffer, 0xff);
    netbufWriteU32(&buffer, sequence);
    if (buffer.error) return -1;
    *size = buffer.wp;
    return 0;
}

int test_enet_two_peer_roundtrip(void) {
    if (enet_initialize() != 0) return 1;
    int result = 0;
    ENetAddress bind_address;
    if (enet_address_set_ip(&bind_address, "::1") != 0) {
        result = 2;
        goto done;
    }
    bind_address.port = 0;
    ENetHost *server = enet_host_create(&bind_address, 2, NET_CHAN_MAX, 0, 0, 0);
    if (!server || !server->address.port) {
        result = 3;
        if (server) enet_host_destroy(server);
        goto done;
    }
    ENetHost *client = enet_host_create(NULL, 1, NET_CHAN_MAX, 0, 0, 0);
    if (!client) {
        result = 4;
        enet_host_destroy(server);
        goto done;
    }
    ENetAddress server_address;
    if (enet_address_set_ip(&server_address, "::1") != 0) {
        result = 5;
        enet_host_destroy(client);
        enet_host_destroy(server);
        goto done;
    }
    server_address.port = server->address.port;
    ENetPeer *client_peer = enet_host_connect(client, &server_address, NET_CHAN_MAX, 0);
    if (!client_peer) {
        result = 6;
        enet_host_destroy(client);
        enet_host_destroy(server);
        goto done;
    }

    enet_host_flush(client);
    ENetPeer *server_peer = NULL;
    int server_connected = 0, client_connected = 0, request_sent = 0, response_received = 0;
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
    for (int attempt = 0; attempt < 5000 && !response_received; attempt++) {
        for (int side = 0; side < 2; side++) {
            ENetHost *host = side == 0 ? server : client;
            ENetEvent event;
            int service_result;
            while ((service_result = enet_host_service(host, &event, 0)) > 0) {
                if (event.type == ENET_EVENT_TYPE_CONNECT) {
                    if (side == 0) { server_peer = event.peer; server_connected = 1; }
                    else client_connected = 1;
                } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    struct netbuf received;
                    netbufStartReadData(&received, event.packet->data, (u32)event.packet->dataLength);
                    uint32_t magic = netbufReadU32(&received);
                    uint16_t version = netbufReadU16(&received);
                    uint8_t type = netbufReadU8(&received);
                    uint8_t slot = netbufReadU8(&received);
                    uint32_t sequence = netbufReadU32(&received);
                    if (received.error || netbufReadLeft(&received) || magic != GEVR_NET_MAGIC ||
                        version != GEVR_NET_VERSION || type != NET_MSG_NONE || slot != 0xff) {
                        result = 7;
                    } else if (side == 0 && sequence == 0x1234abcd) {
                        uint8_t payload[16];
                        size_t payload_size;
                        if (write_header(payload, sizeof(payload), 0x89abcdef, &payload_size) != 0) {
                            result = 8;
                        } else {
                            ENetPacket *packet = enet_packet_create(payload, payload_size, ENET_PACKET_FLAG_RELIABLE);
                            if (!packet || enet_peer_send(server_peer, NET_CHAN_RELIABLE, packet) != 0) {
                                if (packet) enet_packet_destroy(packet);
                                result = 9;
                            }
                        }
                    } else if (side == 1 && sequence == 0x89abcdef) {
                        response_received = 1;
                    } else {
                        result = 10;
                    }
                    enet_packet_destroy(event.packet);
                    if (result) break;
                } else if (event.type == ENET_EVENT_TYPE_DISCONNECT ||
                           event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT) {
                    result = 11;
                    break;
                }
            }
            if (service_result < 0) { fprintf(stderr, "ENet service error side=%d errno=%d\n", side, errno); result = 19; break; }
            if (result) break;
        }
        if (result) break;
        if (server_connected && client_connected && !request_sent) {
            uint8_t payload[16];
            size_t payload_size;
            if (write_header(payload, sizeof(payload), 0x1234abcd, &payload_size) != 0) {
                result = 12;
                break;
            }
            ENetPacket *packet = enet_packet_create(payload, payload_size, ENET_PACKET_FLAG_RELIABLE);
            if (!packet || enet_peer_send(client_peer, NET_CHAN_RELIABLE, packet) != 0) {
                if (packet) enet_packet_destroy(packet);
                result = 13;
                break;
            }
            request_sent = 1;
        }
        enet_host_flush(server);
        enet_host_flush(client);
        nanosleep(&pause, NULL);
    }
    if (!result && !response_received) {
        char server_ip[80] = "?";
        enet_address_get_ip(&server->address, server_ip, sizeof(server_ip));
        fprintf(stderr, "ENet timeout server_connected=%d client_connected=%d sent=%d port=%u ip=%s client_state=%d\n",
                server_connected, client_connected, request_sent, server->address.port, server_ip, client_peer->state);
        result = server_connected ? (client_connected ? 14 : 16) : (client_connected ? 17 : 18);
    }
    enet_peer_disconnect_now(client_peer, 0);
    enet_host_destroy(client);
    enet_host_destroy(server);

done:
    enet_deinitialize();
    return result;
}
