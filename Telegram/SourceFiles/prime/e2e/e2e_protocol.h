/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.

X3DH and the Double Ratchet exactly as PrimeGram for Android does them, and
the same text framing, so that the two clients can talk to each other.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <cstdint>

#include <vector>

namespace Prime::E2E {

inline constexpr auto kMaxSkippedKeys = 200;

struct Session {
	enum class Mode {
		None,
		// This device sent a bundle request and waits for the reply.
		Requested,
		// A bundle request was received and waits for the user's decision.
		IncomingPending,
		Established,
	};

	quint64 selfUserId = 0;
	quint64 peerUserId = 0;
	Mode mode = Mode::None;

	QByteArray peerIkSignPub;
	QByteArray rootKey;
	QByteArray sendChainKey;
	int sendMessageNumber = 0;
	QByteArray recvChainKey;
	int recvMessageNumber = 0;
	int previousSendChainLength = 0;
	QByteArray myRatchetPriv;
	QByteArray myRatchetPub;
	QByteArray theirRatchetPub;
	QByteArray pendingSpkPriv;
	QByteArray pendingSpkPub;
	QByteArray incomingBundleRaw; // The "[PGE2E1:H ...]" text, as received.

	// Insertion ordered, oldest evicted first.
	std::vector<std::pair<QByteArray, QByteArray>> skipped;
};

struct Header {
	QByteArray ratchetPub;
	int previousChainLength = 0;
	int messageNumber = 0;
};

struct Encrypted {
	Header header;
	QByteArray nonce;
	QByteArray ciphertext;
};

// ---- X3DH and the ratchet ----

[[nodiscard]] QByteArray DeriveX3dhSecret(
	const QByteArray &dh1,
	const QByteArray &dh2,
	const QByteArray &dh3,
	const QByteArray &dh4); // dh4 may be empty.

void InitAsInitiator(
	Session &session,
	const QByteArray &sharedSecret,
	const QByteArray &responderSpkPub);
void InitAsResponder(
	Session &session,
	const QByteArray &sharedSecret,
	const QByteArray &spkPriv,
	const QByteArray &spkPub);

// Empty ciphertext on failure.
[[nodiscard]] Encrypted Encrypt(Session &session, const QByteArray &plain);
// Null result when the message cannot be decrypted.
[[nodiscard]] QByteArray Decrypt(
	Session &session,
	const Header &header,
	const QByteArray &nonce,
	const QByteArray &ciphertext);

// ---- Framing ----

inline constexpr auto kSubtypeRequest = 0;
inline constexpr auto kSubtypeResponse = 1;

struct Bundle {
	int subtype = kSubtypeRequest;
	QByteArray ikDhPub;
	QByteArray ikSignPub;
	QByteArray spkPub;
	QByteArray spkSig;
	int spkId = 0;
	std::vector<int> opkIndices;
	std::vector<QByteArray> opkPubs;
	int consumedOpkIndex = -1;
	QByteArray ephemeralPub;
};

struct DecodedCiphertext {
	Header header;
	QByteArray nonce;
	QByteArray ciphertext;
};

[[nodiscard]] bool IsHandshake(const QString &text);
[[nodiscard]] bool IsCiphertext(const QString &text);
[[nodiscard]] bool IsCancel(const QString &text);
[[nodiscard]] bool IsDisableSentinel(const QString &text);

[[nodiscard]] QString EncodeCancel();
[[nodiscard]] QString DisableSentinel();

[[nodiscard]] QString EncodeHandshake(const Bundle &bundle);
[[nodiscard]] bool DecodeHandshake(const QString &text, Bundle &out);
[[nodiscard]] QString EncodeCiphertext(const Encrypted &message);
[[nodiscard]] bool DecodeCiphertext(
	const QString &text,
	DecodedCiphertext &out);

// The six digit code that two people compare out of band.
[[nodiscard]] QString VerificationCode(
	const QByteArray &myIkSignPub,
	const QByteArray &peerIkSignPub,
	const QString &seedPhrase);

} // namespace Prime::E2E
