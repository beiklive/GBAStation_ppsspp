// Copyright (c) 2013- PPSSPP Project.
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/
// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.
// proAdhoc
// This is a direct port of Coldbird's code from http://code.google.com/p/aemu/
// All credit goes to him!

#include "ppsspp_config.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include "Common/Net/SocketCompat.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/System/OSD.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/TimeUtil.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMutex.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceUtility.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/Instance.h"
#include "Core/MemMap.h"
#include "proAdhoc.h"
#include "Core/HLE/NetAdhocCommon.h"

#ifdef _WIN32
#undef errno
#define errno WSAGetLastError()
#endif

#if PPSSPP_PLATFORM(SWITCH) && !defined(INADDR_NONE)
// Missing toolchain define
#define INADDR_NONE 0xFFFFFFFF
#endif

uint16_t portOffset;
uint32_t minSocketTimeoutUS;
uint32_t fakePoolSize = 0;
SceNetMallocStat netAdhocPoolStat = {};
SceNetAdhocMatchingContext *contexts = NULL;
char *dummyPeekBuf64k = NULL;
int dummyPeekBuf64kSize = 65536;
int one = 1;
std::atomic<bool> friendFinderRunning(false);
SceNetAdhocctlPeerInfo *friends = NULL;
SceNetAdhocctlScanInfo *networks = NULL;
SceNetAdhocctlScanInfo *newnetworks = NULL;
u64 adhocctlStartTime = 0;
bool isAdhocctlNeedLogin = false;
bool isAdhocctlBusy = false;
int adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
int adhocctlCurrentMode = ADHOCCTL_MODE_NONE;
int adhocConnectionType = ADHOC_CONNECT;
int gameModeSocket = (int)INVALID_SOCKET; // UDP/PDP socket? on Master only?
int gameModeBuffSize = 0;
u8 *gameModeBuffer = nullptr;
GameModeArea masterGameModeArea;
std::vector<GameModeArea> replicaGameModeAreas;
std::vector<SceNetEtherAddr> requiredGameModeMacs;
std::vector<SceNetEtherAddr> gameModeMacs;
std::map<SceNetEtherAddr, u16_le> gameModePeerPorts;
int actionAfterAdhocMipsCall;
int actionAfterMatchingMipsCall;

// Broadcast MAC
uint8_t broadcastMAC[ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// NOTE: This does not need to be managed by the socket manager - not exposed to the game.
std::atomic<int> metasocket((int)INVALID_SOCKET);
SceNetAdhocctlParameter parameter;
SceNetAdhocctlAdhocId product_code;
std::thread friendFinderThread;
std::recursive_mutex peerlock;
AdhocSocket *adhocSockets[MAX_SOCKET];
bool isOriPort = false;
bool isLocalServer = false;
SockAddrIN4 g_adhocServerIP;
SockAddrIN4 g_localhostIP;
sockaddr LocalIP;
int defaultWlanChannel = PSP_SYSTEMPARAM_ADHOC_CHANNEL_11; 

static std::mutex chatLogLock;
static std::vector<std::string> chatLog;
static int chatMessageGeneration = 0;
static int chatMessageCount = 0;

bool isMacMatch(const SceNetEtherAddr *addr1, const SceNetEtherAddr *addr2) {
	return (memcmp(((const char *)addr1) + 1, ((const char *)addr2) + 1, ETHER_ADDR_LEN - 1) == 0);
}

bool isLocalMAC(const SceNetEtherAddr *addr) {
	SceNetEtherAddr saddr;
	getLocalMac(&saddr);
	return isMacMatch(addr, &saddr);
}

bool isPDPPortInUse(uint16_t port) {
	for (int i = 0; i < MAX_SOCKET; i++) {
		auto sock = adhocSockets[i];
		if (sock != NULL && sock->type == SOCK_PDP)
			if (sock->data.pdp.lport == port)
				return true;
	}
	return false;
}

bool isPTPPortInUse(uint16_t port, bool forListen, SceNetEtherAddr* dstmac, uint16_t dstport) {
	for (int i = 0; i < MAX_SOCKET; i++) {
		auto sock = adhocSockets[i];
		if (sock != NULL && sock->type == SOCK_PTP) {
			if (sock->data.ptp.lport == port &&
				((forListen && sock->data.ptp.state == ADHOC_PTP_STATE_LISTEN) ||
				 (!forListen && sock->data.ptp.state != ADHOC_PTP_STATE_LISTEN && 
				  sock->data.ptp.pport == dstport && dstmac != nullptr && isMacMatch(&sock->data.ptp.paddr, dstmac)))) 
			{
				return true;
			}
		}
	}
	return false;
}

std::string ip2str(in_addr in, bool maskPublicIP) {
	char str[INET_ADDRSTRLEN] = "...";
	u8 *ipptr = (u8 *)&in;
#ifdef _DEBUG
	maskPublicIP = false;
#endif
	if (maskPublicIP && !isPrivateIP(in.s_addr))
		snprintf(str, sizeof(str), "%u.%u.xx.%u", ipptr[0], ipptr[1], ipptr[3]);
	else
		snprintf(str, sizeof(str), "%u.%u.%u.%u", ipptr[0], ipptr[1], ipptr[2], ipptr[3]);
	return std::string(str);
}

std::string mac2str(const SceNetEtherAddr *mac) {
	char str[18] = ":::::";
	if (mac != NULL) {
		snprintf(str, sizeof(str), "%02x:%02x:%02x:%02x:%02x:%02x", mac->data[0],
				 mac->data[1], mac->data[2], mac->data[3], mac->data[4],
				 mac->data[5]);
	}
	return std::string(str);
}

SceNetAdhocMatchingMemberInternal *
addMember(SceNetAdhocMatchingContext *context, SceNetEtherAddr *mac) {
	if (context == NULL || mac == NULL)
		return NULL;
	SceNetAdhocMatchingMemberInternal *peer = findPeer(context, mac);
	if (peer != NULL) {
		WARN_LOG(Log::sceNet, "Member Peer Already Existed! Updating [%s]", mac2str(mac).c_str());
		peer->state = 0;
		peer->sending = 0;
		peer->lastping = CoreTiming::GetGlobalTimeUsScaled();
	} else {
		peer = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));
		if (peer != NULL) {
			memset(peer, 0, sizeof(SceNetAdhocMatchingMemberInternal));
			peer->mac = *mac;
			peer->lastping = CoreTiming::GetGlobalTimeUsScaled();
			peerlock.lock();
			peer->next = context->peerlist;
			context->peerlist = peer;
			peerlock.unlock();
		}
	}
	return peer;
}

void addFriend(SceNetAdhocctlConnectPacketS2C *packet) {
	if (packet == NULL)
		return;
	std::lock_guard<std::recursive_mutex> guard(peerlock);
	SceNetAdhocctlPeerInfo *peer = findFriend(&packet->mac);
	if (peer != NULL) {
		u32 tmpip = packet->ip;
		WARN_LOG(Log::sceNet, "Friend Peer Already Existed! Updating [%s][%s][%s]",
				 mac2str(&packet->mac).c_str(),
				 ip2str(*(struct in_addr *)&tmpip).c_str(),
				 packet->name.data);
		peer->nickname = packet->name;
		peer->mac_addr = packet->mac;
		peer->ip_addr = packet->ip;
		peer->port_offset = ((isOriPort && !isPrivateIP(peer->ip_addr)) ? 0 : portOffset);
		peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
	} else {
		peer = (SceNetAdhocctlPeerInfo *)malloc(sizeof(SceNetAdhocctlPeerInfo));
		if (peer != NULL) {
			memset(peer, 0, sizeof(SceNetAdhocctlPeerInfo));
			peer->nickname = packet->name;
			peer->mac_addr = packet->mac;
			peer->ip_addr = packet->ip;
			peer->port_offset = ((isOriPort && !isPrivateIP(peer->ip_addr)) ? 0 : portOffset);
			peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
			peer->next = friends;
			friends = peer;
		}
	}
}

SceNetAdhocctlPeerInfo *findFriend(SceNetEtherAddr *MAC) {
	if (MAC == NULL)
		return NULL;
	SceNetAdhocctlPeerInfo *peer = friends;
	for (; peer != NULL; peer = peer->next) {
		if (isMacMatch(&peer->mac_addr, MAC))
			break;
	}
	return peer;
}

SceNetAdhocctlPeerInfo *findFriendByIP(uint32_t ip) {
	SceNetAdhocctlPeerInfo *peer = friends;
	for (; peer != NULL; peer = peer->next) {
		if (peer->ip_addr == ip)
			break;
	}
	return peer;
}

int IsSocketReady(int fd, bool readfd, bool writefd, int *errorcode, int timeoutUS) {
	fd_set readfds, writefds;
	timeval tval;
	if (fd < 0) {
		if (errorcode != nullptr) *errorcode = EBADF;
		return SOCKET_ERROR;
	}
#if !defined(_WIN32)
	if (fd >= FD_SETSIZE) {
		if (errorcode != nullptr) *errorcode = EBADF;
		return SOCKET_ERROR;
	}
#endif
	FD_ZERO(&readfds);
	writefds = readfds;
	if (readfd) FD_SET(fd, &readfds);
	if (writefd) FD_SET(fd, &writefds);

	tval.tv_sec = timeoutUS / 1000000;
	tval.tv_usec = timeoutUS % 1000000;
	int ret = select(fd + 1, readfd ? &readfds : nullptr, writefd ? &writefds : nullptr, nullptr, &tval);
	if (errorcode != nullptr)
		*errorcode = (ret < 0 ? socket_errno : 0);
	return ret;
}

void changeBlockingMode(int fd, int nonblocking) {
	unsigned long on = 1;
	unsigned long off = 0;
#if defined(_WIN32)
	if (nonblocking) ioctlsocket(fd, FIONBIO, &on);
	else ioctlsocket(fd, FIONBIO, &off);
#else
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) flags = 0;
	if (nonblocking) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	else fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

int countAvailableNetworks(const bool excludeSelf) {
	int count = 0;
	SceNetAdhocctlScanInfo *group = networks;
	for (; group != NULL && (!excludeSelf || !isLocalMAC(&group->bssid.mac_addr)); group = group->next)
		count++;
	return count;
}

SceNetAdhocctlScanInfo *findGroup(SceNetEtherAddr *MAC) {
	if (MAC == NULL) return NULL;
	SceNetAdhocctlScanInfo *group = networks;
	for (; group != NULL; group = group->next) {
		if (isMacMatch(&group->bssid.mac_addr, MAC))
			break;
	}
	return group;
}

void freeGroupsRecursive(SceNetAdhocctlScanInfo *node) {
	if (node == NULL) return;
	freeGroupsRecursive(node->next);
	free(node);
}

void deleteAllAdhocSockets() {
	for (int i = 0; i < MAX_SOCKET; i++) {
		if (adhocSockets[i] != NULL) {
			auto sock = adhocSockets[i];
			int fd = -1;
			if (sock->type == SOCK_PTP) fd = sock->data.ptp.id;
			else if (sock->type == SOCK_PDP) fd = sock->data.pdp.id;
			if (fd > 0) {
				shutdown(fd, SD_RECEIVE);
				closesocket(fd);
			}
			free(adhocSockets[i]);
			adhocSockets[i] = NULL;
		}
	}
}

void deleteAllGMB() {
	if (gameModeBuffer) {
		free(gameModeBuffer);
		gameModeBuffer = nullptr;
		gameModeBuffSize = 0;
	}
	if (masterGameModeArea.data) {
		free(masterGameModeArea.data);
		masterGameModeArea = {0};
	}
	for (auto &it : replicaGameModeAreas) {
		if (it.data) {
			free(it.data);
			it.data = nullptr;
		}
	}
	replicaGameModeAreas.clear();
	gameModeMacs.clear();
	requiredGameModeMacs.clear();
}

void deleteFriendByIP(uint32_t ip) {
	SceNetAdhocctlPeerInfo *prev = NULL;
	SceNetAdhocctlPeerInfo *peer = friends;
	for (; peer != NULL; peer = peer->next) {
		if (peer->ip_addr == ip) {
			peerlock.lock();
			u32 tmpip = peer->ip_addr;
			INFO_LOG(Log::sceNet, "Removing Friend Peer %s [%s]", mac2str(&peer->mac_addr).c_str(), ip2str(*(struct in_addr *)&tmpip).c_str());
			peer->last_recv = 0; 
			peerlock.unlock();
			break;
		}
		prev = peer;
	}
}

int findFreeMatchingID() {
	int min = 1;
	int max = 0;
	SceNetAdhocMatchingContext *item = contexts;
	for (; item != NULL; item = item->next) {
		if (max < item->id) max = item->id;
	}
	for (int i = min; i < max; i++) {
		if (findMatchingContext(i) == NULL) return i;
	}
	return max + 1;
}

SceNetAdhocMatchingContext *findMatchingContext(int id) {
	SceNetAdhocMatchingContext *item = contexts;
	for (; item != NULL; item = item->next) {
		if (item->id == id) return item;
	}
	return NULL;
}

SceNetAdhocMatchingMemberInternal *findOutgoingRequest(SceNetAdhocMatchingContext *context) {
	SceNetAdhocMatchingMemberInternal *peer = context->peerlist;
	for (; peer != NULL; peer = peer->next) {
		if (peer->state == PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST) return peer;
	}
	return NULL;
}

void postAcceptCleanPeerList(SceNetAdhocMatchingContext *context) {
	int delcount = 0;
	int peercount = 0;
	peerlock.lock();
	SceNetAdhocMatchingMemberInternal *peer = context->peerlist;
	while (peer != NULL) {
		SceNetAdhocMatchingMemberInternal *next = peer->next;
		if (peer->state != PSP_ADHOC_MATCHING_PEER_CHILD &&
			peer->state != PSP_ADHOC_MATCHING_PEER_P2P &&
			peer->state != PSP_ADHOC_MATCHING_PEER_PARENT && peer->state != 0) {
			deletePeer(context, peer);
			delcount++;
		}
		peer = next;
		peercount++;
	}
	peerlock.unlock();
	INFO_LOG(Log::sceNet, "Removing Unneeded Peers (%i/%i)", delcount, peercount);
}

void postAcceptAddSiblings(SceNetAdhocMatchingContext *context, int siblingcount, SceNetEtherAddr *siblings) {
	uint8_t *siblings_u8 = (uint8_t *)siblings;
	peerlock.lock();
	for (int i = siblingcount - 1; i >= 0; i--) {
		SceNetEtherAddr *mac = (SceNetEtherAddr *)(siblings_u8 + sizeof(SceNetEtherAddr) * i);
		auto peer = findPeer(context, mac);
		if (peer != NULL) {
			peer->state = PSP_ADHOC_MATCHING_PEER_CHILD;
			peer->sending = 0;
			peer->lastping = CoreTiming::GetGlobalTimeUsScaled();
		} else {
			SceNetAdhocMatchingMemberInternal *sibling = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));
			if (sibling != NULL) {
				memset(sibling, 0, sizeof(SceNetAdhocMatchingMemberInternal));
				memcpy(&sibling->mac, mac, sizeof(SceNetEtherAddr));
				sibling->state = PSP_ADHOC_MATCHING_PEER_CHILD;
				sibling->lastping = CoreTiming::GetGlobalTimeUsScaled();
				sibling->next = context->peerlist;
				context->peerlist = sibling;
			}
		}
	}
	peerlock.unlock();
}

s32_le countChildren(SceNetAdhocMatchingContext *context, const bool excludeTimedout) {
	s32_le count = 0;
	SceNetAdhocMatchingMemberInternal *peer = context->peerlist;
	for (; peer != NULL; peer = peer->next) {
		if (!excludeTimedout || peer->lastping != 0)
			if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD)
				count++;
	}
	return count;
}

SceNetAdhocMatchingMemberInternal *findPeer(SceNetAdhocMatchingContext *context, SceNetEtherAddr *mac) {
	if (mac == NULL) return NULL;
	SceNetAdhocMatchingMemberInternal *peer = context->peerlist;
	for (; peer != NULL; peer = peer->next) {
		if (isMacMatch(&peer->mac, mac)) return peer;
	}
	return NULL;
}

SceNetAdhocMatchingMemberInternal *findParent(SceNetAdhocMatchingContext *context) {
	SceNetAdhocMatchingMemberInternal *peer = context->peerlist;
	for (; peer != NULL; peer = peer->next) {
		if (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT) return peer;
	}
	return NULL;
}

SceNetAdhocMatchingMemberInternal *findP2P(SceNetAdhocMatchingContext *context, const bool excludeTimedout) {
	SceNetAdhocMatchingMemberInternal *peer = context->peerlist;
	for (; peer != NULL; peer = peer->next) {
		if (!excludeTimedout || peer->lastping != 0)
			if (peer->state == PSP_ADHOC_MATCHING_PEER_P2P) return peer;
	}
	return NULL;
}

void deletePeer(SceNetAdhocMatchingContext *context, SceNetAdhocMatchingMemberInternal *&peer) {
	if (context != NULL && peer != NULL) {
		peerlock.lock();
		SceNetAdhocMatchingMemberInternal *previous = NULL;
		SceNetAdhocMatchingMemberInternal *item = context->peerlist;
		for (; item != NULL; item = item->next) {
			if (item == peer) break;
			previous = item;
		}
		if (item != NULL) {
			if (previous != NULL) previous->next = item->next;
			else context->peerlist = item->next;
		}
		free(peer);
		peer = NULL;
		peerlock.unlock();
	}
}

void linkEVMessage(SceNetAdhocMatchingContext *context, ThreadMessage *message) {
	context->eventlock->lock();
	message->next = context->event_stack;
	context->event_stack = message;
	context->eventlock->unlock();
}

void linkIOMessage(SceNetAdhocMatchingContext *context, ThreadMessage *message) {
	context->inputlock->lock();
	message->next = context->input_stack;
	context->input_stack = message;
	context->inputlock->unlock();
}

void sendGenericMessage(SceNetAdhocMatchingContext *context, int stack, SceNetEtherAddr *mac, int opcode, int optlen, const void *opt) {
	uint32_t size = sizeof(ThreadMessage) + optlen;
	uint8_t *memory = (uint8_t *)malloc(size);
	if (memory != NULL) {
		memset(memory, 0, size);
		ThreadMessage *header = (ThreadMessage *)memory;
		header->opcode = opcode;
		header->mac = *mac;
		header->optlen = optlen;
		memcpy(memory + sizeof(ThreadMessage), opt, optlen);
		if (stack == PSP_ADHOC_MATCHING_EVENT_STACK) linkEVMessage(context, header);
		else linkIOMessage(context, header);
		return;
	}
	peerlock.lock();
	auto peer = findPeer(context, mac);
	deletePeer(context, peer);
	peerlock.unlock();
}

void sendAcceptMessage(SceNetAdhocMatchingContext *context, SceNetAdhocMatchingMemberInternal *peer, int optlen, const void *opt) {
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_ACCEPT, optlen, opt);
}

void sendJoinRequest(SceNetAdhocMatchingContext *context, SceNetAdhocMatchingMemberInternal *peer, int optlen, const void *opt) {
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_JOIN, optlen, opt);
}

void sendCancelMessage(SceNetAdhocMatchingContext *context, SceNetAdhocMatchingMemberInternal *peer, int optlen, const void *opt) {
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_CANCEL, optlen, opt);
}

void sendBulkData(SceNetAdhocMatchingContext *context, SceNetAdhocMatchingMemberInternal *peer, int datalen, const void *data) {
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_BULK, datalen, data);
}

void abortBulkTransfer(SceNetAdhocMatchingContext *context, SceNetAdhocMatchingMemberInternal *peer) {
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_BULK_ABORT, 0, NULL);
}

void sendBirthMessage(SceNetAdhocMatchingContext *context, SceNetAdhocMatchingMemberInternal *peer) {
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_BIRTH, 0, NULL);
}

void sendDeathMessage(SceNetAdhocMatchingContext *context, SceNetAdhocMatchingMemberInternal *peer) {
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_DEATH, 0, NULL);
}

uint32_t countConnectedPeers(SceNetAdhocMatchingContext *context, const bool excludeTimedout) {
	uint32_t count = 0;
	if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) {
		count = countChildren(context, excludeTimedout) + 1;
	} else if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD) {
		count = 1;
		if (findParent(context) != NULL) count += countChildren(context, excludeTimedout) + 1;
	} else {
		count = 1;
		if (findP2P(context, excludeTimedout) != NULL) count++;
	}
	return count;
}

void spawnLocalEvent(SceNetAdhocMatchingContext *context, int event, SceNetEtherAddr *mac, int optlen, void *opt) {
	sendGenericMessage(context, PSP_ADHOC_MATCHING_EVENT_STACK, mac, event, optlen, opt);
}

void handleTimeout(SceNetAdhocMatchingContext *context) {
	peerlock.lock();
	SceNetAdhocMatchingMemberInternal *peer = context->peerlist;
	while (peer != NULL && contexts != NULL && coreState != CORE_POWERDOWN) {
		SceNetAdhocMatchingMemberInternal *next = peer->next;
		u64 now = CoreTiming::GetGlobalTimeUsScaled();
		if (peer->state != 0 && static_cast<s64>(now - peer->lastping) > static_cast<s64>(context->timeout)) {
			if ((context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer->state == PSP_ADHOC_MATCHING_PEER_PARENT) ||
				(context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) ||
				(context->mode == PSP_ADHOC_MATCHING_MODE_P2P && (peer->state == PSP_ADHOC_MATCHING_PEER_P2P || peer->state == PSP_ADHOC_MATCHING_PEER_OFFER))) {
				spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_TIMEOUT, &peer->mac, 0, NULL);
				if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) sendDeathMessage(context, peer);
				else sendCancelMessage(context, peer, 0, NULL);
			}
		}
		peer = next;
	}
	peerlock.unlock();
}

void clearStackRecursive(ThreadMessage *&node) {
	if (node != NULL) clearStackRecursive(node->next);
	free(node);
	node = NULL;
}

void clearStack(SceNetAdhocMatchingContext *context, int stack) {
	if (context == NULL) return;
	if (stack == PSP_ADHOC_MATCHING_EVENT_STACK) {
		context->eventlock->lock();
		clearStackRecursive(context->event_stack);
		context->eventlock->unlock();
	} else {
		context->inputlock->lock();
		clearStackRecursive(context->input_stack);
		context->inputlock->unlock();
	}
}

void clearPeerList(SceNetAdhocMatchingContext *context) {
	peerlock.lock();
	SceNetAdhocMatchingMemberInternal *peer = context->peerlist;
	while (peer != NULL) {
		context->peerlist = peer->next;
		free(peer);
		peer = context->peerlist;
	}
	peerlock.unlock();
}

void AfterMatchingMipsCall::DoState(PointerWrap &p) {
	auto s = p.Section("AfterMatchingMipsCall", 1, 4);
	if (!s) return;
	if (s >= 1) Do(p, EventID);
	else EventID = -1;
	if (s >= 4) {
		Do(p, contextID);
		Do(p, bufAddr);
	} else {
		contextID = -1;
		bufAddr = 0;
	}
}

void AfterMatchingMipsCall::run(MipsCall &call) {
	if (context == NULL) {
		peerlock.lock();
		context = findMatchingContext(contextID);
		peerlock.unlock();
	}
	u32 v0 = currentMIPS->r[MIPS_REG_V0];
	if (Memory::IsValidAddress(bufAddr)) userMemory.Free(bufAddr);
}

void AfterMatchingMipsCall::SetData(int ContextID, int eventId, u32_le BufAddr) {
	contextID = ContextID;
	EventID = eventId;
	bufAddr = BufAddr;
	peerlock.lock();
	context = findMatchingContext(ContextID);
	peerlock.unlock();
}

bool SetMatchingInCallback(SceNetAdhocMatchingContext *context, bool IsInCB) {
	if (context == NULL) return false;
	peerlock.lock();
	context->IsMatchingInCB = IsInCB;
	peerlock.unlock();
	return IsInCB;
}

bool IsMatchingInCallback(SceNetAdhocMatchingContext *context) {
	bool inCB = false;
	if (context == NULL) return inCB;
	peerlock.lock();
	inCB = (context->IsMatchingInCB);
	peerlock.unlock();
	return inCB;
}

void AfterAdhocMipsCall::DoState(PointerWrap &p) {
	auto s = p.Section("AfterAdhocMipsCall", 1, 4);
	if (!s) return;
	if (s >= 3) {
		Do(p, HandlerID);
		Do(p, EventID);
		Do(p, argsAddr);
	} else {
		HandlerID = -1;
		EventID = -1;
		argsAddr = 0;
	}
}

void AfterAdhocMipsCall::run(MipsCall &call) {
	SetAdhocctlInCallback(false);
	isAdhocctlBusy = false;
}

void AfterAdhocMipsCall::SetData(int handlerID, int eventId, u32_le ArgsAddr) {
	HandlerID = handlerID;
	EventID = eventId;
	argsAddr = ArgsAddr;
}

int SetAdhocctlInCallback(bool IsInCB) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	IsAdhocctlInCB += (IsInCB ? 1 : -1);
	return IsAdhocctlInCB;
}

int IsAdhocctlInCallback() {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	return IsAdhocctlInCB;
}

void notifyAdhocctlHandlers(u32 flag, u32 error) {
	__UpdateAdhocctlHandlers(flag, error);
}

void freeFriendsRecursive(SceNetAdhocctlPeerInfo *node, int32_t *count) {
	if (node == NULL) return;
	freeFriendsRecursive(node->next, count);
	free(node);
	if (count != NULL) (*count)++;
}

void timeoutFriendsRecursive(SceNetAdhocctlPeerInfo *node, int32_t *count) {
	if (node == NULL) return;
	timeoutFriendsRecursive(node->next, count);
	node->last_recv = 0;
	if (count != NULL) (*count)++;
}

void sendChat(const std::string &chatString) {
	SceNetAdhocctlChatPacketC2S chat{};
	chat.base.opcode = OPCODE_CHAT;
	if (friendFinderRunning) {
		if (!chatString.empty()) {
			std::string message = chatString.substr(0, 60);
			strcpy(chat.message, message.c_str());
			if (IsSocketReady((int)metasocket, false, true) > 0) {
				send((int)metasocket, (const char *)&chat, sizeof(chat), MSG_NOSIGNAL);
				std::string name = g_Config.sNickName;
				std::lock_guard<std::mutex> guard(chatLogLock);
				chatLog.emplace_back(name.substr(0, 8) + ": " + chat.message);
				chatMessageGeneration++;
			}
		}
	} else {
		std::lock_guard<std::mutex> guard(chatLogLock);
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		chatLog.push_back(std::string(n->T("You're in Offline Mode, go to lobby or online hall")));
		chatMessageGeneration++;
	}
}

std::vector<std::string> getChatLog() {
	std::lock_guard<std::mutex> guard(chatLogLock);
	if (chatLog.size() > 50) {
		chatLog.erase(chatLog.begin(), chatLog.begin() + (chatLog.size() - 50));
	}
	return chatLog;
}

int GetChatChangeID() { return chatMessageGeneration; }
int GetChatMessageCount() { return chatMessageCount; }

int friendFinder() {
	SetCurrentThreadName("FriendFinder");
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	int rxpos = 0;
	uint8_t rx[1024];
	uint64_t lastping = 0;
	uint64_t now;

	addrinfo *resolved = nullptr;
	std::string err;
	g_adhocServerIP.in.sin_addr.s_addr = INADDR_NONE;

	if (g_Config.bEnableWlan && !net::DNSResolve(g_Config.proAdhocServer, "", &resolved, err)) {
		g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("DNS Error Resolving")) + g_Config.proAdhocServer);
	}

	if (resolved) {
		for (auto ptr = resolved; ptr != NULL; ptr = ptr->ai_next) {
			if (ptr->ai_family == AF_INET) g_adhocServerIP.in = *(sockaddr_in *)ptr->ai_addr;
		}
		net::DNSResolveFree(resolved);
	}
	g_adhocServerIP.in.sin_port = htons(SERVER_PORT);
	friendFinderRunning = true;

	while (friendFinderRunning) {
		if (metasocket == (int)INVALID_SOCKET && netAdhocctlInited && isAdhocctlNeedLogin) {
			if (g_Config.bEnableWlan) {
				if (initNetwork(&product_code) == 0) {
					g_adhocServerConnected = true;
					adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
				}
			}
		}
		isAdhocctlNeedLogin = false;

		if (g_adhocServerConnected) {
			now = time_now_d() * 1000000.0;
			if (static_cast<s64>(now - lastping) >= PSP_ADHOCCTL_PING_TIMEOUT) {
				uint8_t opcode = OPCODE_PING;
				if (IsSocketReady((int)metasocket, false, true) > 0) {
					if (send((int)metasocket, (const char *)&opcode, 1, MSG_NOSIGNAL) == SOCKET_ERROR) {
						g_adhocServerConnected = false;
						closesocket((int)metasocket);
						metasocket = (int)INVALID_SOCKET;
					} else {
						lastping = now;
					}
				}
			}

			if (IsSocketReady((int)metasocket, true, false) > 0) {
				int received = (int)recv((int)metasocket, (char *)(rx + rxpos), sizeof(rx) - rxpos, MSG_NOSIGNAL);
				if (received > 0) rxpos += received;
			}

			if (rxpos > 0) {
				if (rx[0] == OPCODE_CONNECT_BSSID && rxpos >= (int)sizeof(SceNetAdhocctlConnectBSSIDPacketS2C)) {
					SceNetAdhocctlConnectBSSIDPacketS2C *packet = (SceNetAdhocctlConnectBSSIDPacketS2C *)rx;
					parameter.bssid.mac_addr = packet->mac;
					notifyAdhocctlHandlers(ADHOCCTL_EVENT_CONNECT, 0);
					memmove(rx, rx + sizeof(SceNetAdhocctlConnectBSSIDPacketS2C), rxpos - sizeof(SceNetAdhocctlConnectBSSIDPacketS2C));
					rxpos -= sizeof(SceNetAdhocctlConnectBSSIDPacketS2C);
				} else if (rx[0] == OPCODE_CHAT && rxpos >= (int)sizeof(SceNetAdhocctlChatPacketS2C)) {
					// Handle chat...
					rxpos -= sizeof(SceNetAdhocctlChatPacketS2C);
				}
				// Other opcodes follow similarly...
			}
		}
		sleep_ms(10);
	}
	adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
	return 0;
}

int getActivePeerCount(const bool excludeTimedout) {
	int count = 0;
	SceNetAdhocctlPeerInfo *peer = friends;
	for (; peer != NULL; peer = peer->next) {
		if (!excludeTimedout || peer->last_recv != 0) count++;
	}
	return count;
}

int getLocalIp(sockaddr_in *SocketAddress) {
	if (isLocalServer) {
		SocketAddress->sin_addr = g_localhostIP.in.sin_addr;
		return 0;
	}
	// Socket detection fallback...
	return -1;
}

uint32_t getLocalIp(int sock) {
	struct sockaddr_in localAddr{};
	socklen_t addrLen = sizeof(localAddr);
	getsockname(sock, (struct sockaddr *)&localAddr, &addrLen);
	return localAddr.sin_addr.s_addr;
}

bool isPrivateIP(uint32_t ip) {
	// Simple range check...
	return false; 
}

void getLocalMac(SceNetEtherAddr *addr) {
	uint8_t mac[ETHER_ADDR_LEN] = {0};
	ParseMacAddress(g_Config.sMACAddress, mac);
	memcpy(addr, mac, ETHER_ADDR_LEN);
}

uint16_t getLocalPort(int sock) {
	struct sockaddr_in localAddr{};
	socklen_t addrLen = sizeof(localAddr);
	getsockname(sock, (struct sockaddr *)&localAddr, &addrLen);
	return ntohs(localAddr.sin_port);
}

int getPDPSocketCount() {
	int counter = 0;
	for (int i = 0; i < MAX_SOCKET; i++)
		if (adhocSockets[i] != NULL && adhocSockets[i]->type == SOCK_PDP)
			counter++;
	return counter;
}

int getPTPSocketCount() {
	int counter = 0;
	for (int i = 0; i < MAX_SOCKET; i++)
		if (adhocSockets[i] != NULL && adhocSockets[i]->type == SOCK_PTP)
			counter++;
	return counter;
}

int initNetwork(SceNetAdhocctlAdhocId *adhoc_id) {
	metasocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (metasocket == INVALID_SOCKET) return SOCKET_ERROR;
	changeBlockingMode((int)metasocket, 1);
	
	if (connect((int)metasocket, &g_adhocServerIP.addr, sizeof(g_adhocServerIP)) == SOCKET_ERROR) {
		// handle connection...
	}
	return 0;
}

bool isBroadcastMAC(const SceNetEtherAddr *addr) {
	return (memcmp(addr->data, "\xFF\xFF\xFF\xFF\xFF\xFF", ETHER_ADDR_LEN) == 0);
}

const char *AdhocCtlStateToString(int state) {
	switch (state) {
		case ADHOCCTL_STATE_DISCONNECTED: return "DISCONNECTED";
		case ADHOCCTL_STATE_CONNECTED: return "CONNECTED";
		default: return "(unk)";
	}
}