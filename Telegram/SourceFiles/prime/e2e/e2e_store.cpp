/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/e2e/e2e_store.h"

#include "prime/prime_store.h"

#include <QtCore/QDataStream>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif // Q_OS_WIN

namespace Prime::E2E {
namespace {

constexpr auto kMagic = quint32(0x50474553); // "PGES"
constexpr auto kVersion = quint32(1);
constexpr auto kOpkBatch = 20;
constexpr auto kSpkLifetime = qint64(7) * 24 * 3600;
constexpr auto kMaxPlaintexts = 20000;
constexpr auto kMaxFieldBytes = 1 << 20;

[[nodiscard]] QString Path() {
	return cWorkingDir() + u"tdata/prime_e2e.bin"_q;
}

[[nodiscard]] QByteArray Seal(const QByteArray &data) {
#ifdef Q_OS_WIN
	auto in = DATA_BLOB{
		DWORD(data.size()),
		reinterpret_cast<BYTE*>(const_cast<char*>(data.constData())),
	};
	auto entropy = QByteArray("PrimeGram E2E store v1");
	auto salt = DATA_BLOB{
		DWORD(entropy.size()),
		reinterpret_cast<BYTE*>(entropy.data()),
	};
	auto out = DATA_BLOB();
	if (!CryptProtectData(
			&in,
			L"PrimeGram E2E",
			&salt,
			nullptr,
			nullptr,
			CRYPTPROTECT_UI_FORBIDDEN,
			&out)) {
		return {};
	}
	auto result = QByteArray(
		reinterpret_cast<const char*>(out.pbData),
		int(out.cbData));
	LocalFree(out.pbData);
	return result;
#else // Q_OS_WIN
	return data;
#endif // Q_OS_WIN
}

[[nodiscard]] QByteArray Unseal(const QByteArray &data) {
#ifdef Q_OS_WIN
	auto in = DATA_BLOB{
		DWORD(data.size()),
		reinterpret_cast<BYTE*>(const_cast<char*>(data.constData())),
	};
	auto entropy = QByteArray("PrimeGram E2E store v1");
	auto salt = DATA_BLOB{
		DWORD(entropy.size()),
		reinterpret_cast<BYTE*>(entropy.data()),
	};
	auto out = DATA_BLOB();
	if (!CryptUnprotectData(
			&in,
			nullptr,
			&salt,
			nullptr,
			nullptr,
			CRYPTPROTECT_UI_FORBIDDEN,
			&out)) {
		return {};
	}
	auto result = QByteArray(
		reinterpret_cast<const char*>(out.pbData),
		int(out.cbData));
	LocalFree(out.pbData);
	return result;
#else // Q_OS_WIN
	return data;
#endif // Q_OS_WIN
}

[[nodiscard]] QByteArray ReadField(QDataStream &stream) {
	auto length = qint32();
	stream >> length;
	if (stream.status() != QDataStream::Ok
		|| length < 0
		|| length > kMaxFieldBytes) {
		stream.setStatus(QDataStream::ReadCorruptData);
		return {};
	}
	auto result = QByteArray(length, Qt::Uninitialized);
	if (stream.readRawData(result.data(), length) != length) {
		stream.setStatus(QDataStream::ReadCorruptData);
		return {};
	}
	return result;
}

void WriteField(QDataStream &stream, const QByteArray &data) {
	stream << qint32(data.size());
	stream.writeRawData(data.constData(), data.size());
}

void WriteKeyPair(QDataStream &stream, const KeyPair &pair) {
	WriteField(stream, pair.priv);
	WriteField(stream, pair.pub);
}

[[nodiscard]] KeyPair ReadKeyPair(QDataStream &stream) {
	auto result = KeyPair();
	result.priv = ReadField(stream);
	result.pub = ReadField(stream);
	return result;
}

void WriteSession(QDataStream &stream, const Session &s) {
	stream << quint64(s.selfUserId) << quint64(s.peerUserId)
		<< qint32(s.mode);
	WriteField(stream, s.peerIkSignPub);
	WriteField(stream, s.rootKey);
	WriteField(stream, s.sendChainKey);
	stream << qint32(s.sendMessageNumber);
	WriteField(stream, s.recvChainKey);
	stream << qint32(s.recvMessageNumber)
		<< qint32(s.previousSendChainLength);
	WriteField(stream, s.myRatchetPriv);
	WriteField(stream, s.myRatchetPub);
	WriteField(stream, s.theirRatchetPub);
	WriteField(stream, s.pendingSpkPriv);
	WriteField(stream, s.pendingSpkPub);
	WriteField(stream, s.incomingBundleRaw);
	stream << qint32(s.skipped.size());
	for (const auto &[id, key] : s.skipped) {
		WriteField(stream, id);
		WriteField(stream, key);
	}
}

[[nodiscard]] bool ReadSession(QDataStream &stream, Session &s) {
	auto self = quint64();
	auto peer = quint64();
	auto mode = qint32();
	stream >> self >> peer >> mode;
	s.selfUserId = self;
	s.peerUserId = peer;
	s.mode = Session::Mode(mode);
	s.peerIkSignPub = ReadField(stream);
	s.rootKey = ReadField(stream);
	s.sendChainKey = ReadField(stream);
	auto sendNumber = qint32();
	stream >> sendNumber;
	s.sendMessageNumber = sendNumber;
	s.recvChainKey = ReadField(stream);
	auto recvNumber = qint32();
	auto previous = qint32();
	stream >> recvNumber >> previous;
	s.recvMessageNumber = recvNumber;
	s.previousSendChainLength = previous;
	s.myRatchetPriv = ReadField(stream);
	s.myRatchetPub = ReadField(stream);
	s.theirRatchetPub = ReadField(stream);
	s.pendingSpkPriv = ReadField(stream);
	s.pendingSpkPub = ReadField(stream);
	s.incomingBundleRaw = ReadField(stream);
	auto count = qint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| count < 0
		|| count > kMaxSkippedKeys) {
		return false;
	}
	for (auto i = 0; i != count; ++i) {
		auto id = ReadField(stream);
		auto key = ReadField(stream);
		s.skipped.emplace_back(std::move(id), std::move(key));
	}
	return stream.status() == QDataStream::Ok;
}

} // namespace

Store::Store() = default;

Identity &Store::identity() {
	if (!_loaded) {
		load();
	}
	ensureIdentity();
	rotateSpkIfNeeded();
	topUpOpks();
	return *_identity;
}

void Store::ensureIdentity() {
	if (_identity && !_identity->ikDh.priv.isEmpty()) {
		return;
	}
	_identity = std::make_unique<Identity>();
	_identity->ikDh = GenerateX25519();
	_identity->ikSign = GenerateEd25519();
	_identity->spk = GenerateX25519();
	_identity->spkSig = Ed25519Sign(
		_identity->ikSign.priv,
		_identity->spk.pub);
	_identity->spkId = 1;
	_identity->spkCreated = QDateTime::currentSecsSinceEpoch();
	save();
}

void Store::rotateSpkIfNeeded() {
	const auto now = QDateTime::currentSecsSinceEpoch();
	if (now - _identity->spkCreated < kSpkLifetime) {
		return;
	}
	_identity->spk = GenerateX25519();
	_identity->spkSig = Ed25519Sign(
		_identity->ikSign.priv,
		_identity->spk.pub);
	++_identity->spkId;
	_identity->spkCreated = now;
	save();
}

void Store::topUpOpks() {
	if (int(_identity->opks.size()) >= kOpkBatch / 2) {
		return;
	}
	while (int(_identity->opks.size()) < kOpkBatch) {
		_identity->opks.emplace(
			_identity->nextOpkIndex++,
			GenerateX25519());
	}
	save();
}

Bundle Store::LocalBundle(int subtype) {
	const auto &id = identity();
	auto result = Bundle();
	result.subtype = subtype;
	result.ikDhPub = id.ikDh.pub;
	result.ikSignPub = id.ikSign.pub;
	result.spkPub = id.spk.pub;
	result.spkSig = id.spkSig;
	result.spkId = id.spkId;
	for (const auto &[index, pair] : id.opks) {
		result.opkIndices.push_back(index);
		result.opkPubs.push_back(pair.pub);
	}
	return result;
}

KeyPair Store::OpkIfUnconsumed(qint32 index) {
	const auto &id = identity();
	const auto i = id.opks.find(index);
	return (i != id.opks.end()) ? i->second : KeyPair();
}

void Store::consumeOpk(qint32 index) {
	identity();
	_identity->opks.erase(index);
	save();
}

QByteArray Store::ownIkSignPub() {
	return identity().ikSign.pub;
}

Session *Store::session(quint64 self, quint64 peer) {
	if (!_loaded) {
		load();
	}
	const auto i = _sessions.find({ self, peer });
	return (i != _sessions.end()) ? &i->second : nullptr;
}

Session &Store::ensureSession(quint64 self, quint64 peer) {
	if (!_loaded) {
		load();
	}
	auto &result = _sessions[{ self, peer }];
	result.selfUserId = self;
	result.peerUserId = peer;
	return result;
}

void Store::removeSession(quint64 self, quint64 peer) {
	if (!_loaded) {
		load();
	}
	_sessions.erase({ self, peer });
	save();
}

bool Store::plaintext(
		quint64 self,
		quint64 peer,
		qint64 msgId,
		QString &out) const {
	const auto i = _plain.find({ self, peer, msgId });
	if (i == _plain.end()) {
		return false;
	}
	out = i->second;
	return true;
}

void Store::rememberPlaintext(
		quint64 self,
		quint64 peer,
		qint64 msgId,
		const QString &text) {
	if (!_loaded) {
		load();
	}
	const auto key = std::make_tuple(self, peer, msgId);
	if (_plain.emplace(key, text).second) {
		_plainOrder.push_back(key);
		while (int(_plainOrder.size()) > kMaxPlaintexts) {
			_plain.erase(_plainOrder.front());
			_plainOrder.erase(_plainOrder.begin());
		}
	} else {
		_plain[key] = text;
	}
	save();
}

void Store::save() {
	_dirty = true;
	if (_onDirty) {
		_onDirty();
	}
}

void Store::flush() {
	if (!_loaded || !_dirty) {
		return;
	}
	_dirty = false;
	auto raw = QByteArray();
	{
		QDataStream stream(&raw, QIODevice::WriteOnly);
		stream.setByteOrder(QDataStream::BigEndian);
		stream << kMagic << kVersion;
		stream << qint8(_identity ? 1 : 0);
		if (_identity) {
			WriteKeyPair(stream, _identity->ikDh);
			WriteKeyPair(stream, _identity->ikSign);
			WriteKeyPair(stream, _identity->spk);
			WriteField(stream, _identity->spkSig);
			stream << _identity->spkId << _identity->spkCreated
				<< _identity->nextOpkIndex
				<< qint32(_identity->opks.size());
			for (const auto &[index, pair] : _identity->opks) {
				stream << index;
				WriteKeyPair(stream, pair);
			}
		}
		stream << qint32(_sessions.size());
		for (const auto &[key, session] : _sessions) {
			WriteSession(stream, session);
		}
		stream << qint32(_plainOrder.size());
		for (const auto &key : _plainOrder) {
			const auto i = _plain.find(key);
			if (i == _plain.end()) {
				continue;
			}
			stream << quint64(std::get<0>(key))
				<< quint64(std::get<1>(key))
				<< qint64(std::get<2>(key))
				<< i->second;
		}
	}
	const auto sealed = Seal(raw);
	if (sealed.isEmpty()) {
		return;
	}
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QFile(Path());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(sealed);
	}
}

void Store::load() {
	_loaded = true;
	auto file = QFile(Path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	auto raw = Unseal(file.readAll());
	if (raw.isEmpty()) {
		return;
	}
	QDataStream stream(&raw, QIODevice::ReadOnly);
	stream.setByteOrder(QDataStream::BigEndian);
	auto magic = quint32();
	auto version = quint32();
	stream >> magic >> version;
	if (magic != kMagic || version != kVersion) {
		return;
	}
	auto hasIdentity = qint8();
	stream >> hasIdentity;
	if (hasIdentity) {
		auto id = std::make_unique<Identity>();
		id->ikDh = ReadKeyPair(stream);
		id->ikSign = ReadKeyPair(stream);
		id->spk = ReadKeyPair(stream);
		id->spkSig = ReadField(stream);
		auto count = qint32();
		stream >> id->spkId >> id->spkCreated >> id->nextOpkIndex >> count;
		if (stream.status() != QDataStream::Ok || count < 0 || count > 1000) {
			return;
		}
		for (auto i = 0; i != count; ++i) {
			auto index = qint32();
			stream >> index;
			id->opks.emplace(index, ReadKeyPair(stream));
		}
		if (stream.status() != QDataStream::Ok) {
			return;
		}
		_identity = std::move(id);
	}
	auto sessions = qint32();
	stream >> sessions;
	if (stream.status() != QDataStream::Ok || sessions < 0 || sessions > 100000) {
		return;
	}
	for (auto i = 0; i != sessions; ++i) {
		auto session = Session();
		if (!ReadSession(stream, session)) {
			return;
		}
		_sessions[{ session.selfUserId, session.peerUserId }]
			= std::move(session);
	}
	auto plains = qint32();
	stream >> plains;
	if (stream.status() != QDataStream::Ok || plains < 0) {
		return;
	}
	for (auto i = 0; i != plains; ++i) {
		auto self = quint64();
		auto peer = quint64();
		auto id = qint64();
		auto text = QString();
		stream >> self >> peer >> id >> text;
		if (stream.status() != QDataStream::Ok) {
			return;
		}
		const auto key = std::make_tuple(self, peer, id);
		if (_plain.emplace(key, text).second) {
			_plainOrder.push_back(key);
		}
	}
}

void Store::wipe() {
	_identity = nullptr;
	_sessions.clear();
	_plain.clear();
	_plainOrder.clear();
	_loaded = true;
	_dirty = false;
	QFile::remove(Path());
}

} // namespace Prime::E2E
