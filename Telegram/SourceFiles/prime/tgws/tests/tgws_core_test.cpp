// Standalone cross-check of the TgWs core against vectors produced by the
// original Python implementation (see gen_vectors.py).
//
//   cl /std:c++17 /EHsc /I<SourceFiles> /I<openssl include> tgws_core_test.cpp
//      ..\tgws_constants.cpp ..\tgws_crypto.cpp ..\tgws_splitter.cpp
//      ..\tgws_ws_frame.cpp libcrypto.lib ws2_32.lib crypt32.lib user32.lib
//      advapi32.lib
#include "prime/tgws/tgws_crypto.h"
#include "prime/tgws/tgws_splitter.h"
#include "prime/tgws/tgws_ws_frame.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace Prime::TgWs;

namespace {

int failures = 0;

#define CHECK(cond, ...) \
	do { \
		if (!(cond)) { \
			++failures; \
			std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
			std::printf(__VA_ARGS__); \
			std::printf("\n"); \
		} \
	} while (false)

Bytes FromHex(const std::string &hex) {
	auto result = Bytes();
	for (auto i = size_t(0); i + 1 < hex.size(); i += 2) {
		result.push_back(uint8_t(std::stoi(hex.substr(i, 2), nullptr, 16)));
	}
	return result;
}

std::vector<Bytes> FromHexList(const std::string &list) {
	auto result = std::vector<Bytes>();
	auto stream = std::stringstream(list);
	auto item = std::string();
	while (std::getline(stream, item, ',')) {
		result.push_back(FromHex(item));
	}
	return result;
}

std::string ToHex(const Bytes &data) {
	static const char *digits = "0123456789abcdef";
	auto result = std::string();
	for (const auto byte : data) {
		result += digits[byte >> 4];
		result += digits[byte & 15];
	}
	return result;
}

template <size_t N>
std::array<uint8_t, N> ToArray(const Bytes &data) {
	auto result = std::array<uint8_t, N>();
	if (data.size() == N) {
		std::copy(data.begin(), data.end(), result.begin());
	} else {
		++failures;
		std::printf("FAIL size mismatch %zu != %zu\n", data.size(), N);
	}
	return result;
}

void CheckCase(
		const std::map<std::string, std::string> &v,
		const Secret &secret,
		int index) {
	const auto key = [&](const char *name) {
		const auto full = "c" + std::to_string(index) + "_" + name;
		const auto i = v.find(full);
		if (i == v.end()) {
			std::printf("missing %s\n", full.c_str());
			std::exit(2);
		}
		return i->second;
	};

	const auto handshake = ToArray<kHandshakeLen>(FromHex(key("handshake")));
	const auto relayInit = ToArray<kHandshakeLen>(FromHex(key("relay_init")));
	const auto dc = std::stoi(key("dc"));
	const auto media = std::stoi(key("media")) != 0;
	const auto proto = uint32_t(std::stoul(key("proto")));

	const auto parsed = ParseClientInit(handshake, secret);
	CHECK(parsed.has_value(), "case %d: init rejected", index);
	if (!parsed) {
		return;
	}
	CHECK(parsed->dcId == dc, "case %d: dc %d != %d", index, parsed->dcId, dc);
	CHECK(parsed->isMedia == media, "case %d: media", index);
	CHECK(parsed->protoInt() == proto, "case %d: proto", index);

	auto wrongSecret = secret;
	wrongSecret[0] ^= 0x55;
	CHECK(!ParseClientInit(handshake, wrongSecret).has_value(),
		"case %d: wrong secret accepted", index);

	auto ctx = BuildCryptoCtx(*parsed, secret, relayInit);

	const auto upClient = FromHexList(key("up_client"));
	const auto upTg = FromHexList(key("up_tg"));
	CHECK(upClient.size() == upTg.size(), "case %d: vector sizes", index);
	for (auto i = size_t(0); i != upClient.size(); ++i) {
		const auto plain = ctx.clientDecrypt->process(upClient[i]);
		const auto out = ctx.relayEncrypt->process(plain);
		CHECK(out == upTg[i], "case %d: uplink packet %zu\n  got  %s\n  want %s",
			index, i, ToHex(out).c_str(), ToHex(upTg[i]).c_str());
	}

	auto joined = Bytes();
	for (const auto &part : upTg) {
		joined.insert(joined.end(), part.begin(), part.end());
	}
	const auto cuts = std::vector<size_t>{ 0, 7, 20, joined.size() };
	auto splitter = MsgSplitter(relayInit, proto);
	auto parts = std::vector<Bytes>();
	for (auto i = size_t(0); i + 1 < cuts.size(); ++i) {
		auto piece = splitter.split(
			joined.data() + cuts[i],
			cuts[i + 1] - cuts[i]);
		for (auto &p : piece) {
			parts.push_back(std::move(p));
		}
	}
	for (auto &p : splitter.flush()) {
		parts.push_back(std::move(p));
	}
	const auto wantParts = FromHexList(key("up_parts"));
	CHECK(parts == wantParts, "case %d: split parts differ (%zu vs %zu)",
		index, parts.size(), wantParts.size());

	const auto downTg = FromHexList(key("down_tg"));
	const auto downClient = FromHexList(key("down_client"));
	CHECK(downTg.size() == downClient.size(), "case %d: down sizes", index);
	for (auto i = size_t(0); i != downTg.size(); ++i) {
		const auto plain = ctx.relayDecrypt->process(downTg[i]);
		const auto out = ctx.clientEncrypt->process(plain);
		CHECK(out == downClient[i], "case %d: downlink packet %zu", index, i);
	}

	for (auto round = 0; round != 50; ++round) {
		const auto dcIdx = media ? -dc : dc;
		const auto init = GenerateRelayInit(parsed->protoTag, dcIdx);
		auto probe = AesCtr(
			init.data() + kSkipLen,
			init.data() + kSkipLen + kPrekeyLen);
		auto plain = Handshake();
		probe.process(init.data(), plain.data(), kHandshakeLen);
		const auto tag = std::array<uint8_t, 4>{
			plain[56], plain[57], plain[58], plain[59]
		};
		const auto raw = int16_t(plain[60] | (plain[61] << 8));
		CHECK(tag == parsed->protoTag, "case %d: generated tag", index);
		CHECK(raw == dcIdx, "case %d: generated dc %d != %d", index, raw, dcIdx);
	}
}

void CheckWebSocket() {
	for (const auto size : { 0, 5, 125, 126, 127, 65535, 65536, 70000 }) {
		auto payload = Bytes(size);
		for (auto i = 0; i != size; ++i) {
			payload[i] = uint8_t(i * 7 + 3);
		}
		const auto frame = Ws::BuildFrame(Ws::kOpBinary, payload);
		CHECK((frame[0] & 0x0F) == Ws::kOpBinary, "ws opcode");
		CHECK((frame[1] & 0x80) != 0, "ws client frames are masked");

		auto parser = Ws::FrameParser();
		const auto half = frame.size() / 2;
		parser.append(frame.data(), half);
		CHECK(!parser.next().has_value() || size == 0 || half >= frame.size(),
			"ws partial frame must wait");
		parser.append(frame.data() + half, frame.size() - half);
		const auto parsed = parser.next();
		CHECK(parsed.has_value(), "ws frame size %d not parsed", size);
		if (parsed) {
			CHECK(parsed->payload == payload, "ws payload size %d", size);
			CHECK(!parser.next().has_value(), "ws extra frame");
		}
	}

	const auto response = std::string(
		"HTTP/1.1 302 Found\r\nLocation: https://example.org/x\r\n"
		"Server: cloudflare\r\n\r\ntrailing");
	const auto parsed = Ws::ParseUpgradeResponse(
		reinterpret_cast<const uint8_t*>(response.data()),
		response.size());
	CHECK(parsed.has_value(), "upgrade response");
	if (parsed) {
		CHECK(parsed->status == 302 && parsed->isRedirect(), "status");
		CHECK(parsed->location == "https://example.org/x", "location");
		CHECK(response.substr(parsed->consumed) == "trailing", "consumed");
	}
	const auto partial = std::string("HTTP/1.1 101 Switching\r\nUpgr");
	CHECK(!Ws::ParseUpgradeResponse(
		reinterpret_cast<const uint8_t*>(partial.data()),
		partial.size()).has_value(), "partial header must wait");

	const auto request = Ws::BuildUpgradeRequest("kws2.web.telegram.org", "/apiws");
	CHECK(request.text.find("GET /apiws HTTP/1.1\r\n") == 0, "request line");
	CHECK(request.text.find("Host: kws2.web.telegram.org\r\n") != std::string::npos, "host");
	CHECK(request.key.size() == 24, "key size %zu", request.key.size());
}

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		std::printf("usage: tgws_core_test <vectors.txt>\n");
		return 2;
	}
	auto vectors = std::map<std::string, std::string>();
	auto file = std::ifstream(argv[1]);
	auto line = std::string();
	while (std::getline(file, line)) {
		const auto space = line.find(' ');
		if (space != std::string::npos) {
			vectors[line.substr(0, space)] = line.substr(space + 1);
		}
	}
	const auto secret = ToArray<kSecretLen>(FromHex(vectors["secret"]));
	for (auto index = 0; index != 3; ++index) {
		CheckCase(vectors, secret, index);
	}
	CheckWebSocket();
	std::printf(failures ? "FAILED: %d\n" : "ALL OK\n", failures);
	return failures ? 1 : 0;
}
