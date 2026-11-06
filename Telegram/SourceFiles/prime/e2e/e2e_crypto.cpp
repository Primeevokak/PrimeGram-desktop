/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/e2e/e2e_crypto.h"

extern "C" {
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
} // extern "C"

#include <QtCore/QRandomGenerator>

#include <memory>

namespace Prime::E2E {
namespace {

template <typename Callback>
struct Finally {
	explicit Finally(Callback callback) : callback(std::move(callback)) {
	}
	~Finally() {
		callback();
	}
	Callback callback;
};

struct KeyDeleter {
	void operator()(EVP_PKEY *key) const {
		EVP_PKEY_free(key);
	}
};
using KeyPointer = std::unique_ptr<EVP_PKEY, KeyDeleter>;

struct ContextDeleter {
	void operator()(EVP_PKEY_CTX *context) const {
		EVP_PKEY_CTX_free(context);
	}
};
using ContextPointer = std::unique_ptr<EVP_PKEY_CTX, ContextDeleter>;

[[nodiscard]] const unsigned char *U(const QByteArray &bytes) {
	return reinterpret_cast<const unsigned char*>(bytes.constData());
}

[[nodiscard]] KeyPair Generate(int type) {
	auto context = ContextPointer(EVP_PKEY_CTX_new_id(type, nullptr));
	if (!context || EVP_PKEY_keygen_init(context.get()) != 1) {
		return {};
	}
	auto raw = (EVP_PKEY*)nullptr;
	if (EVP_PKEY_keygen(context.get(), &raw) != 1 || !raw) {
		return {};
	}
	const auto key = KeyPointer(raw);
	auto result = KeyPair();
	auto size = size_t(kKeyLen);
	result.priv = QByteArray(kKeyLen, Qt::Uninitialized);
	if (EVP_PKEY_get_raw_private_key(
			key.get(),
			reinterpret_cast<unsigned char*>(result.priv.data()),
			&size) != 1 || size != kKeyLen) {
		return {};
	}
	size = kKeyLen;
	result.pub = QByteArray(kKeyLen, Qt::Uninitialized);
	if (EVP_PKEY_get_raw_public_key(
			key.get(),
			reinterpret_cast<unsigned char*>(result.pub.data()),
			&size) != 1 || size != kKeyLen) {
		return {};
	}
	return result;
}

} // namespace

QByteArray RandomBytes(int count) {
	auto result = QByteArray(count, Qt::Uninitialized);
	RAND_bytes(reinterpret_cast<unsigned char*>(result.data()), count);
	return result;
}

KeyPair GenerateX25519() {
	return Generate(EVP_PKEY_X25519);
}

QByteArray X25519(const QByteArray &priv, const QByteArray &pub) {
	if (priv.size() != kKeyLen || pub.size() != kKeyLen) {
		return {};
	}
	const auto own = KeyPointer(EVP_PKEY_new_raw_private_key(
		EVP_PKEY_X25519,
		nullptr,
		U(priv),
		priv.size()));
	const auto peer = KeyPointer(EVP_PKEY_new_raw_public_key(
		EVP_PKEY_X25519,
		nullptr,
		U(pub),
		pub.size()));
	if (!own || !peer) {
		return {};
	}
	const auto context = ContextPointer(EVP_PKEY_CTX_new(own.get(), nullptr));
	if (!context
		|| EVP_PKEY_derive_init(context.get()) != 1
		|| EVP_PKEY_derive_set_peer(context.get(), peer.get()) != 1) {
		return {};
	}
	auto size = size_t(kKeyLen);
	auto result = QByteArray(kKeyLen, Qt::Uninitialized);
	if (EVP_PKEY_derive(
			context.get(),
			reinterpret_cast<unsigned char*>(result.data()),
			&size) != 1 || size != kKeyLen) {
		return {};
	}
	return result;
}

KeyPair GenerateEd25519() {
	return Generate(EVP_PKEY_ED25519);
}

QByteArray Ed25519Sign(const QByteArray &priv, const QByteArray &message) {
	if (priv.size() != kKeyLen) {
		return {};
	}
	const auto key = KeyPointer(EVP_PKEY_new_raw_private_key(
		EVP_PKEY_ED25519,
		nullptr,
		U(priv),
		priv.size()));
	if (!key) {
		return {};
	}
	const auto context = EVP_MD_CTX_new();
	const auto guard = Finally([&] { EVP_MD_CTX_free(context); });
	if (!context
		|| EVP_DigestSignInit(
			context,
			nullptr,
			nullptr,
			nullptr,
			key.get()) != 1) {
		return {};
	}
	auto size = size_t(kSigLen);
	auto result = QByteArray(kSigLen, Qt::Uninitialized);
	if (EVP_DigestSign(
			context,
			reinterpret_cast<unsigned char*>(result.data()),
			&size,
			U(message),
			message.size()) != 1 || size != kSigLen) {
		return {};
	}
	return result;
}

bool Ed25519Verify(
		const QByteArray &pub,
		const QByteArray &message,
		const QByteArray &signature) {
	if (pub.size() != kKeyLen || signature.size() != kSigLen) {
		return false;
	}
	const auto key = KeyPointer(EVP_PKEY_new_raw_public_key(
		EVP_PKEY_ED25519,
		nullptr,
		U(pub),
		pub.size()));
	if (!key) {
		return false;
	}
	const auto context = EVP_MD_CTX_new();
	const auto guard = Finally([&] { EVP_MD_CTX_free(context); });
	if (!context
		|| EVP_DigestVerifyInit(
			context,
			nullptr,
			nullptr,
			nullptr,
			key.get()) != 1) {
		return false;
	}
	return EVP_DigestVerify(
		context,
		U(signature),
		signature.size(),
		U(message),
		message.size()) == 1;
}

QByteArray HmacSha256(const QByteArray &key, const QByteArray &data) {
	auto result = QByteArray(32, Qt::Uninitialized);
	auto size = unsigned(32);
	// An empty key is allowed by HMAC; OpenSSL wants a valid pointer.
	static const unsigned char empty = 0;
	const auto keyData = key.isEmpty() ? &empty : U(key);
	if (!HMAC(
			EVP_sha256(),
			keyData,
			key.size(),
			U(data),
			size_t(data.size()),
			reinterpret_cast<unsigned char*>(result.data()),
			&size)) {
		return {};
	}
	return result;
}

QByteArray HkdfSha256(
		const QByteArray &salt,
		const QByteArray &ikm,
		const QByteArray &info,
		int length) {
	// RFC 5869: extract, then expand. An empty salt means a block of zeros.
	const auto prk = HmacSha256(
		salt.isEmpty() ? QByteArray(32, '\0') : salt,
		ikm);
	if (prk.isEmpty() || length > 255 * 32) {
		return {};
	}
	auto result = QByteArray();
	auto previous = QByteArray();
	for (auto counter = 1; result.size() < length; ++counter) {
		auto block = previous;
		block += info;
		block.append(char(counter));
		previous = HmacSha256(prk, block);
		if (previous.isEmpty()) {
			return {};
		}
		result += previous;
	}
	result.truncate(length);
	return result;
}

QByteArray AesGcmEncrypt(
		const QByteArray &key,
		const QByteArray &nonce,
		const QByteArray &aad,
		const QByteArray &plaintext) {
	if (key.size() != kKeyLen || nonce.size() != kNonceLen) {
		return {};
	}
	const auto context = EVP_CIPHER_CTX_new();
	const auto guard = Finally([&] { EVP_CIPHER_CTX_free(context); });
	if (!context) {
		return {};
	}
	auto result = QByteArray(plaintext.size() + kTagLen, Qt::Uninitialized);
	auto out = reinterpret_cast<unsigned char*>(result.data());
	auto length = 0;
	auto full = 0;
	auto ok = EVP_EncryptInit_ex(
			context,
			EVP_aes_256_gcm(),
			nullptr,
			nullptr,
			nullptr) == 1
		&& EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_IVLEN,
			kNonceLen,
			nullptr) == 1
		&& EVP_EncryptInit_ex(
			context,
			nullptr,
			nullptr,
			U(key),
			U(nonce)) == 1;
	if (ok && !aad.isEmpty()) {
		ok = EVP_EncryptUpdate(
			context,
			nullptr,
			&length,
			U(aad),
			aad.size()) == 1;
	}
	if (ok && !plaintext.isEmpty()) {
		ok = EVP_EncryptUpdate(
			context,
			out,
			&length,
			U(plaintext),
			plaintext.size()) == 1;
		full = length;
	}
	ok = ok
		&& EVP_EncryptFinal_ex(context, out + full, &length) == 1
		&& EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_GET_TAG,
			kTagLen,
			out + plaintext.size()) == 1;
	return ok ? result : QByteArray();
}

QByteArray AesGcmDecrypt(
		const QByteArray &key,
		const QByteArray &nonce,
		const QByteArray &aad,
		const QByteArray &ciphertext) {
	if (key.size() != kKeyLen
		|| nonce.size() != kNonceLen
		|| ciphertext.size() < kTagLen) {
		return {};
	}
	const auto size = ciphertext.size() - kTagLen;
	const auto context = EVP_CIPHER_CTX_new();
	const auto guard = Finally([&] { EVP_CIPHER_CTX_free(context); });
	if (!context) {
		return {};
	}
	auto result = QByteArray(size, Qt::Uninitialized);
	auto out = reinterpret_cast<unsigned char*>(result.data());
	auto length = 0;
	auto full = 0;
	auto tag = QByteArray(ciphertext.constData() + size, kTagLen);
	auto ok = EVP_DecryptInit_ex(
			context,
			EVP_aes_256_gcm(),
			nullptr,
			nullptr,
			nullptr) == 1
		&& EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_IVLEN,
			kNonceLen,
			nullptr) == 1
		&& EVP_DecryptInit_ex(
			context,
			nullptr,
			nullptr,
			U(key),
			U(nonce)) == 1;
	if (ok && !aad.isEmpty()) {
		ok = EVP_DecryptUpdate(
			context,
			nullptr,
			&length,
			U(aad),
			aad.size()) == 1;
	}
	if (ok && size > 0) {
		ok = EVP_DecryptUpdate(
			context,
			out,
			&length,
			U(ciphertext),
			size) == 1;
		full = length;
	}
	ok = ok
		&& EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_TAG,
			kTagLen,
			tag.data()) == 1
		&& EVP_DecryptFinal_ex(context, out + full, &length) == 1;
	if (!ok) {
		return {};
	}
	// A valid empty plaintext must stay distinguishable from a failure.
	return result.isNull() ? QByteArray("") : result;
}

void Wipe(QByteArray &data) {
	if (!data.isEmpty()) {
		OPENSSL_cleanse(data.data(), data.size());
	}
	data.clear();
}

} // namespace Prime::E2E
