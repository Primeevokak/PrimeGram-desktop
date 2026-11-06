/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <stdexcept>

namespace Prime::TgWs {
namespace {

constexpr auto kDerivedKeyInputLen = kPrekeyLen + kSecretLen;

[[nodiscard]] bool IsReservedStart(const Handshake &init) {
	if (init[0] == 0xEF) {
		return true;
	}
	const auto first = (uint32_t(init[0]) << 24)
		| (uint32_t(init[1]) << 16)
		| (uint32_t(init[2]) << 8)
		| uint32_t(init[3]);
	switch (first) {
	case 0x48454144U: // HEAD
	case 0x504F5354U: // POST
	case 0x47455420U: // GET
	case 0xEEEEEEEEU:
	case 0xDDDDDDDDU:
	case 0x16030102U: // TLS ClientHello record
		return true;
	}
	return init[4] == 0 && init[5] == 0 && init[6] == 0 && init[7] == 0;
}

[[nodiscard]] std::array<uint8_t, kKeyLen> DeriveKey(
		const uint8_t *prekey32,
		const Secret &secret) {
	auto input = std::array<uint8_t, kDerivedKeyInputLen>();
	std::copy(prekey32, prekey32 + kPrekeyLen, input.begin());
	std::copy(secret.begin(), secret.end(), input.begin() + kPrekeyLen);
	const auto hash = Sha256(input.data(), input.size());
	auto result = std::array<uint8_t, kKeyLen>();
	std::copy(hash.begin(), hash.end(), result.begin());
	return result;
}

} // namespace

struct AesCtr::Impl {
	EVP_CIPHER_CTX *ctx = nullptr;
};

AesCtr::AesCtr(const uint8_t *key32, const uint8_t *iv16)
: _impl(std::make_unique<Impl>()) {
	_impl->ctx = EVP_CIPHER_CTX_new();
	if (!_impl->ctx
		|| EVP_EncryptInit_ex(
			_impl->ctx,
			EVP_aes_256_ctr(),
			nullptr,
			key32,
			iv16) != 1) {
		if (_impl->ctx) {
			EVP_CIPHER_CTX_free(_impl->ctx);
		}
		throw std::runtime_error("TgWs: AES-CTR init failed.");
	}
}

AesCtr::~AesCtr() {
	if (_impl && _impl->ctx) {
		EVP_CIPHER_CTX_free(_impl->ctx);
	}
}

void AesCtr::process(const uint8_t *in, uint8_t *out, size_t size) {
	while (size > 0) {
		const auto chunk = int(std::min<size_t>(size, 1 << 30));
		auto written = 0;
		if (EVP_EncryptUpdate(_impl->ctx, out, &written, in, chunk) != 1
			|| written != chunk) {
			throw std::runtime_error("TgWs: AES-CTR update failed.");
		}
		in += chunk;
		out += chunk;
		size -= chunk;
	}
}

Bytes AesCtr::process(const uint8_t *in, size_t size) {
	auto result = Bytes(size);
	process(in, result.data(), size);
	return result;
}

Bytes AesCtr::process(const Bytes &data) {
	return process(data.data(), data.size());
}

void AesCtr::skip(size_t size) {
	auto zeros = Bytes(size);
	auto sink = Bytes(size);
	process(zeros.data(), sink.data(), size);
}

uint32_t ClientInit::protoInt() const {
	return (uint32_t(protoTag[0]) << 24)
		| (uint32_t(protoTag[1]) << 16)
		| (uint32_t(protoTag[2]) << 8)
		| uint32_t(protoTag[3]);
}

CryptoCtx::CryptoCtx(
	std::unique_ptr<AesCtr> clientDecrypt,
	std::unique_ptr<AesCtr> clientEncrypt,
	std::unique_ptr<AesCtr> relayEncrypt,
	std::unique_ptr<AesCtr> relayDecrypt)
: clientDecrypt(std::move(clientDecrypt))
, clientEncrypt(std::move(clientEncrypt))
, relayEncrypt(std::move(relayEncrypt))
, relayDecrypt(std::move(relayDecrypt)) {
}

void RandomBytes(uint8_t *data, size_t size) {
	if (RAND_bytes(data, int(size)) != 1) {
		throw std::runtime_error("TgWs: RAND_bytes failed.");
	}
}

Secret RandomSecret() {
	auto result = Secret();
	RandomBytes(result.data(), result.size());
	return result;
}

Bytes Sha256(const uint8_t *data, size_t size) {
	auto result = Bytes(SHA256_DIGEST_LENGTH);
	SHA256(data, size, result.data());
	return result;
}

std::optional<ClientInit> ParseClientInit(
		const Handshake &handshake,
		const Secret &secret) {
	const auto prekeyIv = handshake.data() + kSkipLen;
	const auto key = DeriveKey(prekeyIv, secret);
	auto decryptor = AesCtr(key.data(), prekeyIv + kPrekeyLen);
	auto decrypted = Handshake();
	decryptor.process(handshake.data(), decrypted.data(), kHandshakeLen);

	auto tag = std::array<uint8_t, 4>();
	std::copy(
		decrypted.begin() + kProtoTagPos,
		decrypted.begin() + kProtoTagPos + 4,
		tag.begin());
	if (tag != kProtoTagAbridged
		&& tag != kProtoTagIntermediate
		&& tag != kProtoTagPadded) {
		return std::nullopt;
	}

	const auto raw = uint16_t(decrypted[kDcIdxPos])
		| (uint16_t(decrypted[kDcIdxPos + 1]) << 8);
	const auto dcIdx = int(int16_t(raw));

	auto result = ClientInit();
	result.dcId = (dcIdx < 0) ? -dcIdx : dcIdx;
	result.isMedia = (dcIdx < 0);
	result.protoTag = tag;
	std::copy(
		prekeyIv,
		prekeyIv + kPrekeyLen + kIvLen,
		result.prekeyIv.begin());
	return result;
}

Handshake GenerateRelayInit(
		const std::array<uint8_t, 4> &protoTag,
		int dcIdx) {
	auto random = Handshake();
	do {
		RandomBytes(random.data(), random.size());
	} while (IsReservedStart(random));

	auto encryptor = AesCtr(
		random.data() + kSkipLen,
		random.data() + kSkipLen + kPrekeyLen);
	auto encrypted = Handshake();
	encryptor.process(random.data(), encrypted.data(), kHandshakeLen);

	const auto dc = uint16_t(int16_t(dcIdx));
	auto tail = std::array<uint8_t, 8>();
	std::copy(protoTag.begin(), protoTag.end(), tail.begin());
	tail[4] = uint8_t(dc & 0xFF);
	tail[5] = uint8_t(dc >> 8);
	RandomBytes(tail.data() + 6, 2);

	auto result = random;
	for (auto i = 0; i != 8; ++i) {
		const auto keystream = encrypted[kProtoTagPos + i]
			^ random[kProtoTagPos + i];
		result[kProtoTagPos + i] = tail[i] ^ uint8_t(keystream);
	}
	return result;
}

CryptoCtx BuildCryptoCtx(
		const ClientInit &client,
		const Secret &secret,
		const Handshake &relayInit) {
	const auto &prekeyIv = client.prekeyIv;

	const auto clientDecKey = DeriveKey(prekeyIv.data(), secret);
	auto clientDecrypt = std::make_unique<AesCtr>(
		clientDecKey.data(),
		prekeyIv.data() + kPrekeyLen);
	clientDecrypt->skip(kHandshakeLen);

	auto reversed = prekeyIv;
	std::reverse(reversed.begin(), reversed.end());
	const auto clientEncKey = DeriveKey(reversed.data(), secret);
	auto clientEncrypt = std::make_unique<AesCtr>(
		clientEncKey.data(),
		reversed.data() + kPrekeyLen);

	auto relayEncrypt = std::make_unique<AesCtr>(
		relayInit.data() + kSkipLen,
		relayInit.data() + kSkipLen + kPrekeyLen);
	relayEncrypt->skip(kHandshakeLen);

	auto relayReversed = std::array<uint8_t, kPrekeyLen + kIvLen>();
	std::copy(
		relayInit.begin() + kSkipLen,
		relayInit.begin() + kSkipLen + kPrekeyLen + kIvLen,
		relayReversed.begin());
	std::reverse(relayReversed.begin(), relayReversed.end());
	auto relayDecrypt = std::make_unique<AesCtr>(
		relayReversed.data(),
		relayReversed.data() + kPrekeyLen);

	return CryptoCtx(
		std::move(clientDecrypt),
		std::move(clientEncrypt),
		std::move(relayEncrypt),
		std::move(relayDecrypt));
}

} // namespace Prime::TgWs
