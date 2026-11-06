/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.

The cryptography is the same as in PrimeGram for Android (X25519, Ed25519,
HKDF-SHA256, HMAC-SHA256, AES-256-GCM), so that encrypted chats work
between the two clients.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace Prime::E2E {

inline constexpr auto kKeyLen = 32;
inline constexpr auto kSigLen = 64;
inline constexpr auto kNonceLen = 12;
inline constexpr auto kTagLen = 16;

struct KeyPair {
	QByteArray priv;
	QByteArray pub;
};

[[nodiscard]] QByteArray RandomBytes(int count);

[[nodiscard]] KeyPair GenerateX25519();
// Raw scalar multiplication; empty on failure. The result must go through
// HKDF before it is used as a key.
[[nodiscard]] QByteArray X25519(
	const QByteArray &priv,
	const QByteArray &pub);

[[nodiscard]] KeyPair GenerateEd25519();
[[nodiscard]] QByteArray Ed25519Sign(
	const QByteArray &priv,
	const QByteArray &message);
[[nodiscard]] bool Ed25519Verify(
	const QByteArray &pub,
	const QByteArray &message,
	const QByteArray &signature);

[[nodiscard]] QByteArray HkdfSha256(
	const QByteArray &salt,
	const QByteArray &ikm,
	const QByteArray &info,
	int length);
[[nodiscard]] QByteArray HmacSha256(
	const QByteArray &key,
	const QByteArray &data);

// Output is ciphertext followed by the 16 byte tag.
[[nodiscard]] QByteArray AesGcmEncrypt(
	const QByteArray &key,
	const QByteArray &nonce,
	const QByteArray &aad,
	const QByteArray &plaintext);
// Empty (null) result on authentication failure.
[[nodiscard]] QByteArray AesGcmDecrypt(
	const QByteArray &key,
	const QByteArray &nonce,
	const QByteArray &aad,
	const QByteArray &ciphertext);

void Wipe(QByteArray &data);

} // namespace Prime::E2E
