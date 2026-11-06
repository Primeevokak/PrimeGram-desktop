/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.

TgWs transport is a C++ re-implementation of the MIT-licensed
Flowseal/tg-ws-proxy (https://github.com/Flowseal/tg-ws-proxy).
*/
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Prime::TgWs {

inline constexpr auto kHandshakeLen = 64;
inline constexpr auto kSkipLen = 8;
inline constexpr auto kPrekeyLen = 32;
inline constexpr auto kKeyLen = 32;
inline constexpr auto kIvLen = 16;
inline constexpr auto kProtoTagPos = 56;
inline constexpr auto kDcIdxPos = 60;
inline constexpr auto kSecretLen = 16;

inline constexpr std::array<uint8_t, 4> kProtoTagAbridged = {
	0xEF, 0xEF, 0xEF, 0xEF
};
inline constexpr std::array<uint8_t, 4> kProtoTagIntermediate = {
	0xEE, 0xEE, 0xEE, 0xEE
};
inline constexpr std::array<uint8_t, 4> kProtoTagPadded = {
	0xDD, 0xDD, 0xDD, 0xDD
};

inline constexpr uint32_t kProtoAbridgedInt = 0xEFEFEFEFU;
inline constexpr uint32_t kProtoIntermediateInt = 0xEEEEEEEEU;
inline constexpr uint32_t kProtoPaddedInt = 0xDDDDDDDDU;

inline constexpr auto kDcRedirectIp = "149.154.167.220";
inline constexpr auto kFrontingSni = "sprinthost.ru";

inline constexpr auto kWsPoolMaxAgeMs = 120'000;
inline constexpr auto kCfWorkerPoolMaxAgeMs = 100'000;
inline constexpr auto kIpFailCooldownMs = 3'600'000;
inline constexpr auto kDcFailCooldownMs = 60'000;
inline constexpr auto kWsFailTimeoutMs = 2'000;
inline constexpr auto kWsConnectTimeoutMs = 5'000;
inline constexpr auto kFrontingCooldownMs = 1'800'000;

inline constexpr auto kMaxFrameLen = 16ULL * 1024 * 1024;

struct DcInfo {
	int id = 0;
	bool isMedia = false;
};

[[nodiscard]] std::string DefaultIpForDc(int dcId);

[[nodiscard]] std::vector<std::string> WsDomainsForDc(
	int dcId,
	bool isMedia);

[[nodiscard]] std::string RedirectIpForDc(int dcId);

} // namespace Prime::TgWs
