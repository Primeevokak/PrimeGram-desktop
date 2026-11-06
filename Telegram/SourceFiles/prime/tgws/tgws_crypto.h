/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/tgws/tgws_constants.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Prime::TgWs {

using Bytes = std::vector<uint8_t>;
using Handshake = std::array<uint8_t, kHandshakeLen>;
using Secret = std::array<uint8_t, kSecretLen>;

class AesCtr final {
public:
	AesCtr(const uint8_t *key32, const uint8_t *iv16);
	~AesCtr();

	AesCtr(const AesCtr &other) = delete;
	AesCtr &operator=(const AesCtr &other) = delete;

	void process(const uint8_t *in, uint8_t *out, size_t size);
	[[nodiscard]] Bytes process(const uint8_t *in, size_t size);
	[[nodiscard]] Bytes process(const Bytes &data);

	void skip(size_t size);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;

};

struct ClientInit {
	int dcId = 0;
	bool isMedia = false;
	std::array<uint8_t, 4> protoTag = {};
	std::array<uint8_t, kPrekeyLen + kIvLen> prekeyIv = {};

	[[nodiscard]] uint32_t protoInt() const;
};

struct CryptoCtx {
	CryptoCtx(
		std::unique_ptr<AesCtr> clientDecrypt,
		std::unique_ptr<AesCtr> clientEncrypt,
		std::unique_ptr<AesCtr> relayEncrypt,
		std::unique_ptr<AesCtr> relayDecrypt);

	std::unique_ptr<AesCtr> clientDecrypt;
	std::unique_ptr<AesCtr> clientEncrypt;
	std::unique_ptr<AesCtr> relayEncrypt;
	std::unique_ptr<AesCtr> relayDecrypt;
};

[[nodiscard]] Secret RandomSecret();
[[nodiscard]] Bytes Sha256(const uint8_t *data, size_t size);
void RandomBytes(uint8_t *data, size_t size);

[[nodiscard]] std::optional<ClientInit> ParseClientInit(
	const Handshake &handshake,
	const Secret &secret);

[[nodiscard]] Handshake GenerateRelayInit(
	const std::array<uint8_t, 4> &protoTag,
	int dcIdx);

[[nodiscard]] CryptoCtx BuildCryptoCtx(
	const ClientInit &client,
	const Secret &secret,
	const Handshake &relayInit);

} // namespace Prime::TgWs
