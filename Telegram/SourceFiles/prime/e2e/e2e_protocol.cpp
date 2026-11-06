/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/e2e/e2e_protocol.h"

#include "prime/e2e/e2e_crypto.h"

#include <QtCore/QBuffer>
#include <QtCore/QDataStream>

namespace Prime::E2E {
namespace {

const auto kInfoRoot = QByteArray("PGE2E-DR-ROOT-v1");
const auto kInfoX3dh = QByteArray("PGE2E-X3DH-v1");
const auto kInfoNonce = QByteArray("PGE2E-NONCE-v1");
const auto kZeroSalt = QByteArray(32, '\0');
const auto kConstMessageKey = QByteArray(1, char(0x01));
const auto kConstNextChain = QByteArray(1, char(0x02));

const auto kHandshakePrefix = QString::fromLatin1("[PGE2E1:H ");
const auto kCiphertextPrefix = QString::fromLatin1("[PGE2E1:M ");
const auto kCancelMarker = QString::fromLatin1("[PGE2E1:C]");
const auto kDisableSentinel = QString::fromLatin1("PGE2E-DISABLE-v1");
const auto kSuffix = QString::fromLatin1("]");

constexpr auto kMaxFieldBytes = 8192;

[[nodiscard]] QByteArray ConstAppend(
		const QByteArray &a,
		const QByteArray &b) {
	auto result = a;
	result += b;
	return result;
}

// ---- Root and chain KDFs ----

struct RootAndChain {
	QByteArray root;
	QByteArray chain;
};

[[nodiscard]] RootAndChain KdfRootChain(
		const QByteArray &rootKey,
		const QByteArray &dhOutput) {
	const auto out = HkdfSha256(rootKey, dhOutput, kInfoRoot, 64);
	return { out.left(32), out.mid(32, 32) };
}

[[nodiscard]] QByteArray NextChainKey(const QByteArray &chainKey) {
	return HmacSha256(chainKey, kConstNextChain);
}

[[nodiscard]] QByteArray MessageKeyFromChain(const QByteArray &chainKey) {
	return HmacSha256(chainKey, kConstMessageKey);
}

[[nodiscard]] QByteArray NonceForMessageKey(const QByteArray &messageKey) {
	return HkdfSha256(kZeroSalt, messageKey, kInfoNonce, kNonceLen);
}

// Both sides compute the same AAD: the pair of user ids in a canonical
// order, then the message number, all big endian.
[[nodiscard]] QByteArray Aad(const Session &session, int messageNumber) {
	const auto a = std::min(session.selfUserId, session.peerUserId);
	const auto b = std::max(session.selfUserId, session.peerUserId);
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setByteOrder(QDataStream::BigEndian);
	stream << qint64(a) << qint64(b) << qint32(messageNumber);
	return result;
}

[[nodiscard]] QByteArray SkippedId(
		const QByteArray &ratchetPub,
		int messageNumber) {
	return ratchetPub.toHex() + ':' + QByteArray::number(messageNumber);
}

void StoreSkipped(
		Session &session,
		const QByteArray &ratchetPub,
		int messageNumber,
		const QByteArray &messageKey) {
	if (int(session.skipped.size()) >= kMaxSkippedKeys) {
		session.skipped.erase(session.skipped.begin());
	}
	session.skipped.emplace_back(
		SkippedId(ratchetPub, messageNumber),
		messageKey);
}

[[nodiscard]] QByteArray TakeSkipped(
		Session &session,
		const QByteArray &ratchetPub,
		int messageNumber) {
	const auto id = SkippedId(ratchetPub, messageNumber);
	for (auto i = session.skipped.begin(); i != session.skipped.end(); ++i) {
		if (i->first == id) {
			auto key = i->second;
			session.skipped.erase(i);
			return key;
		}
	}
	return {};
}

void SkipMessageKeys(
		Session &session,
		const QByteArray &ratchetPub,
		int untilMessageNumber) {
	if (session.recvChainKey.isEmpty() || ratchetPub.isEmpty()) {
		return;
	}
	while (session.recvMessageNumber < untilMessageNumber) {
		const auto messageKey = MessageKeyFromChain(session.recvChainKey);
		session.recvChainKey = NextChainKey(session.recvChainKey);
		StoreSkipped(
			session,
			ratchetPub,
			session.recvMessageNumber,
			messageKey);
		++session.recvMessageNumber;
	}
}

void DhRatchetStep(Session &session, const QByteArray &newTheirRatchetPub) {
	session.previousSendChainLength = session.sendMessageNumber;
	session.sendMessageNumber = 0;
	session.recvMessageNumber = 0;
	session.theirRatchetPub = newTheirRatchetPub;

	const auto dhRecv = X25519(
		session.myRatchetPriv,
		session.theirRatchetPub);
	const auto receiving = KdfRootChain(session.rootKey, dhRecv);
	session.rootKey = receiving.root;
	session.recvChainKey = receiving.chain;

	const auto fresh = GenerateX25519();
	session.myRatchetPriv = fresh.priv;
	session.myRatchetPub = fresh.pub;

	const auto dhSend = X25519(
		session.myRatchetPriv,
		session.theirRatchetPub);
	const auto sending = KdfRootChain(session.rootKey, dhSend);
	session.rootKey = sending.root;
	session.sendChainKey = sending.chain;
}

// ---- Length prefixed fields ----

void WriteBytes(QDataStream &stream, const QByteArray &data) {
	stream << qint32(data.size());
	stream.writeRawData(data.constData(), data.size());
}

[[nodiscard]] bool ReadBytes(QDataStream &stream, QByteArray &data) {
	auto length = qint32();
	stream >> length;
	if (stream.status() != QDataStream::Ok
		|| length < 0
		|| length > kMaxFieldBytes) {
		return false;
	}
	data.resize(length);
	return stream.readRawData(data.data(), length) == length;
}

[[nodiscard]] bool ReadFixed(
		QDataStream &stream,
		QByteArray &data,
		int length) {
	return ReadBytes(stream, data) && data.size() == length;
}

// Standard base64 with padding and no line breaks.
[[nodiscard]] QString Base64(const QByteArray &data) {
	return QString::fromLatin1(data.toBase64());
}

[[nodiscard]] QByteArray FromBase64(const QString &text) {
	return QByteArray::fromBase64(
		text.toLatin1(),
		QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
}

} // namespace

QByteArray DeriveX3dhSecret(
		const QByteArray &dh1,
		const QByteArray &dh2,
		const QByteArray &dh3,
		const QByteArray &dh4) {
	auto ikm = dh1 + dh2 + dh3;
	if (!dh4.isEmpty()) {
		ikm += dh4;
	}
	const auto result = HkdfSha256(kZeroSalt, ikm, kInfoX3dh, 32);
	Wipe(ikm);
	return result;
}

void InitAsInitiator(
		Session &session,
		const QByteArray &sharedSecret,
		const QByteArray &responderSpkPub) {
	const auto ratchet = GenerateX25519();
	session.myRatchetPriv = ratchet.priv;
	session.myRatchetPub = ratchet.pub;
	session.theirRatchetPub = responderSpkPub;

	const auto dhOut = X25519(session.myRatchetPriv, session.theirRatchetPub);
	const auto rootAndChain = KdfRootChain(sharedSecret, dhOut);
	session.rootKey = rootAndChain.root;
	session.sendChainKey = rootAndChain.chain;
	session.recvChainKey = QByteArray();
	session.sendMessageNumber = 0;
	session.recvMessageNumber = 0;
	session.previousSendChainLength = 0;
	session.mode = Session::Mode::Established;
}

void InitAsResponder(
		Session &session,
		const QByteArray &sharedSecret,
		const QByteArray &spkPriv,
		const QByteArray &spkPub) {
	session.myRatchetPriv = spkPriv;
	session.myRatchetPub = spkPub;
	session.theirRatchetPub = QByteArray();
	session.rootKey = sharedSecret;
	session.sendChainKey = QByteArray();
	session.recvChainKey = QByteArray();
	session.sendMessageNumber = 0;
	session.recvMessageNumber = 0;
	session.previousSendChainLength = 0;
	session.mode = Session::Mode::Established;
}

Encrypted Encrypt(Session &session, const QByteArray &plain) {
	if (session.sendChainKey.isEmpty()) {
		return {};
	}
	auto messageKey = MessageKeyFromChain(session.sendChainKey);
	session.sendChainKey = NextChainKey(session.sendChainKey);

	auto result = Encrypted();
	result.header.ratchetPub = session.myRatchetPub;
	result.header.previousChainLength = session.previousSendChainLength;
	result.header.messageNumber = session.sendMessageNumber;
	++session.sendMessageNumber;

	result.nonce = NonceForMessageKey(messageKey);
	result.ciphertext = AesGcmEncrypt(
		messageKey,
		result.nonce,
		Aad(session, result.header.messageNumber),
		plain);
	Wipe(messageKey);
	return result;
}

QByteArray Decrypt(
		Session &session,
		const Header &header,
		const QByteArray &nonce,
		const QByteArray &ciphertext) {
	auto skipped = TakeSkipped(
		session,
		header.ratchetPub,
		header.messageNumber);
	if (!skipped.isEmpty()) {
		auto plain = AesGcmDecrypt(
			skipped,
			nonce,
			Aad(session, header.messageNumber),
			ciphertext);
		Wipe(skipped);
		return plain;
	}
	if (session.theirRatchetPub.isEmpty()
		|| header.ratchetPub != session.theirRatchetPub) {
		// A new ratchet key from the other side: first keep the keys of
		// the old receiving chain that did not arrive yet, then step.
		if (!session.recvChainKey.isEmpty()) {
			SkipMessageKeys(
				session,
				session.theirRatchetPub,
				header.previousChainLength);
		}
		DhRatchetStep(session, header.ratchetPub);
	}
	SkipMessageKeys(session, header.ratchetPub, header.messageNumber);
	if (session.recvChainKey.isEmpty()) {
		return {};
	}

	auto messageKey = MessageKeyFromChain(session.recvChainKey);
	session.recvChainKey = NextChainKey(session.recvChainKey);
	++session.recvMessageNumber;

	auto plain = AesGcmDecrypt(
		messageKey,
		nonce,
		Aad(session, header.messageNumber),
		ciphertext);
	Wipe(messageKey);
	return plain;
}

bool IsHandshake(const QString &text) {
	return text.startsWith(kHandshakePrefix) && text.endsWith(kSuffix);
}

bool IsCiphertext(const QString &text) {
	return text.startsWith(kCiphertextPrefix) && text.endsWith(kSuffix);
}

bool IsCancel(const QString &text) {
	return text == kCancelMarker;
}

bool IsDisableSentinel(const QString &text) {
	return text == kDisableSentinel;
}

QString EncodeCancel() {
	return kCancelMarker;
}

QString DisableSentinel() {
	return kDisableSentinel;
}

QString EncodeHandshake(const Bundle &bundle) {
	auto bytes = QByteArray();
	QDataStream out(&bytes, QIODevice::WriteOnly);
	out.setByteOrder(QDataStream::BigEndian);
	out << qint8(bundle.subtype);
	WriteBytes(out, bundle.ikDhPub);
	WriteBytes(out, bundle.ikSignPub);
	WriteBytes(out, bundle.spkPub);
	WriteBytes(out, bundle.spkSig);
	out << qint32(bundle.spkId);
	out << qint32(bundle.opkIndices.size());
	for (auto i = size_t(0); i != bundle.opkIndices.size(); ++i) {
		out << qint32(bundle.opkIndices[i]);
		WriteBytes(out, bundle.opkPubs[i]);
	}
	out << qint32(bundle.consumedOpkIndex);
	WriteBytes(out, bundle.ephemeralPub);
	return kHandshakePrefix + Base64(bytes) + kSuffix;
}

bool DecodeHandshake(const QString &text, Bundle &out) {
	if (!IsHandshake(text)) {
		return false;
	}
	const auto raw = FromBase64(text.mid(
		kHandshakePrefix.size(),
		text.size() - kHandshakePrefix.size() - kSuffix.size()));
	if (raw.isEmpty()) {
		return false;
	}
	auto bytes = raw;
	QDataStream in(&bytes, QIODevice::ReadOnly);
	in.setByteOrder(QDataStream::BigEndian);
	auto result = Bundle();
	auto subtype = qint8();
	in >> subtype;
	result.subtype = subtype;
	if (!ReadFixed(in, result.ikDhPub, kKeyLen)
		|| !ReadFixed(in, result.ikSignPub, kKeyLen)
		|| !ReadFixed(in, result.spkPub, kKeyLen)
		|| !ReadFixed(in, result.spkSig, kSigLen)) {
		return false;
	}
	auto spkId = qint32();
	auto count = qint32();
	in >> spkId >> count;
	if (in.status() != QDataStream::Ok || count < 0 || count > 200) {
		return false;
	}
	result.spkId = spkId;
	for (auto i = 0; i != count; ++i) {
		auto index = qint32();
		auto pub = QByteArray();
		in >> index;
		if (in.status() != QDataStream::Ok || !ReadFixed(in, pub, kKeyLen)) {
			return false;
		}
		result.opkIndices.push_back(index);
		result.opkPubs.push_back(pub);
	}
	auto consumed = qint32();
	in >> consumed;
	result.consumedOpkIndex = consumed;
	auto ephemeral = QByteArray();
	if (ReadBytes(in, ephemeral) && ephemeral.size() == kKeyLen) {
		result.ephemeralPub = ephemeral;
	}
	if (in.status() != QDataStream::Ok) {
		return false;
	}
	out = std::move(result);
	return true;
}

QString EncodeCiphertext(const Encrypted &message) {
	auto bytes = QByteArray();
	QDataStream out(&bytes, QIODevice::WriteOnly);
	out.setByteOrder(QDataStream::BigEndian);
	WriteBytes(out, message.header.ratchetPub);
	out << qint32(message.header.previousChainLength);
	out << qint32(message.header.messageNumber);
	WriteBytes(out, message.nonce);
	out.writeRawData(
		message.ciphertext.constData(),
		message.ciphertext.size());
	return kCiphertextPrefix + Base64(bytes) + kSuffix;
}

bool DecodeCiphertext(const QString &text, DecodedCiphertext &out) {
	if (!IsCiphertext(text)) {
		return false;
	}
	const auto raw = FromBase64(text.mid(
		kCiphertextPrefix.size(),
		text.size() - kCiphertextPrefix.size() - kSuffix.size()));
	if (raw.isEmpty()) {
		return false;
	}
	auto bytes = raw;
	QDataStream in(&bytes, QIODevice::ReadOnly);
	in.setByteOrder(QDataStream::BigEndian);
	auto result = DecodedCiphertext();
	if (!ReadFixed(in, result.header.ratchetPub, kKeyLen)) {
		return false;
	}
	auto previous = qint32();
	auto number = qint32();
	in >> previous >> number;
	if (in.status() != QDataStream::Ok) {
		return false;
	}
	result.header.previousChainLength = previous;
	result.header.messageNumber = number;
	if (!ReadFixed(in, result.nonce, kNonceLen)) {
		return false;
	}
	const auto position = raw.size() - int(in.device()->bytesAvailable());
	result.ciphertext = raw.mid(position);
	if (result.ciphertext.size() < kTagLen) {
		return false;
	}
	out = std::move(result);
	return true;
}

QString VerificationCode(
		const QByteArray &myIkSignPub,
		const QByteArray &peerIkSignPub,
		const QString &seedPhrase) {
	if (myIkSignPub.isEmpty() || peerIkSignPub.isEmpty()) {
		return QString();
	}
	auto a = myIkSignPub;
	auto b = peerIkSignPub;
	// Unsigned byte-wise comparison, the shorter one first on a tie.
	const auto greater = [&] {
		const auto size = std::min(a.size(), b.size());
		for (auto i = 0; i != size; ++i) {
			const auto x = uchar(a[i]);
			const auto y = uchar(b[i]);
			if (x != y) {
				return x > y;
			}
		}
		return a.size() > b.size();
	}();
	if (greater) {
		std::swap(a, b);
	}
	const auto fingerprint = HkdfSha256(
		kZeroSalt,
		ConstAppend(a, b),
		QByteArray("PGE2E-FP-v1"),
		32);
	const auto seedKey = HkdfSha256(
		kZeroSalt,
		seedPhrase.toUtf8(),
		QByteArray("PGE2E-SEED-v1"),
		32);
	const auto code = HmacSha256(seedKey, fingerprint);
	if (code.size() < 4) {
		return QString();
	}
	const auto numeric = (quint64(uchar(code[0])) << 24
		| quint64(uchar(code[1])) << 16
		| quint64(uchar(code[2])) << 8
		| quint64(uchar(code[3]))) % 1'000'000;
	return QString::number(numeric).rightJustified(6, '0');
}

} // namespace Prime::E2E
