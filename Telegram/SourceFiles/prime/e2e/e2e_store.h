/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/e2e/e2e_crypto.h"
#include "prime/e2e/e2e_protocol.h"

#include <functional>
#include <map>
#include <memory>

namespace Prime::E2E {

// The long-term keys of this installation: two identity keys, a signed
// prekey that rotates, and a batch of one-time prekeys.
struct Identity {
	KeyPair ikDh;
	KeyPair ikSign;
	KeyPair spk;
	QByteArray spkSig;
	qint32 spkId = 0;
	qint64 spkCreated = 0; // Unix seconds.
	std::map<qint32, KeyPair> opks; // Index to key, unconsumed only.
	qint32 nextOpkIndex = 0;
};

// Everything the E2E feature keeps on disk, sealed so that another user of
// this computer (or a copied file) cannot read it.
class Store final {
public:
	Store();

	[[nodiscard]] Identity &identity();
	[[nodiscard]] Bundle LocalBundle(int subtype);
	[[nodiscard]] KeyPair OpkIfUnconsumed(qint32 index);
	void consumeOpk(qint32 index);
	[[nodiscard]] QByteArray ownIkSignPub();

	[[nodiscard]] Session *session(quint64 self, quint64 peer);
	[[nodiscard]] Session &ensureSession(quint64 self, quint64 peer);
	void removeSession(quint64 self, quint64 peer);

	// Decrypted text of messages, by peer and message id: a ratchet key is
	// used once, so an already decrypted message must never be decrypted
	// again when the chat history is loaded the next time.
	[[nodiscard]] bool plaintext(
		quint64 self,
		quint64 peer,
		qint64 msgId,
		QString &out) const;
	void rememberPlaintext(
		quint64 self,
		quint64 peer,
		qint64 msgId,
		const QString &text);

	// Marks the state as changed; flush() writes it to disk.
	void save();
	void flush();
	// Called whenever the state changes, so that it can be written soon.
	void setDirtyCallback(std::function<void()> callback) {
		_onDirty = std::move(callback);
	}
	void wipe();

private:
	void load();
	void ensureIdentity();
	void rotateSpkIfNeeded();
	void topUpOpks();

	std::unique_ptr<Identity> _identity;
	std::map<std::pair<quint64, quint64>, Session> _sessions;
	std::map<std::tuple<quint64, quint64, qint64>, QString> _plain;
	std::vector<std::tuple<quint64, quint64, qint64>> _plainOrder;
	bool _loaded = false;
	bool _dirty = false;
	std::function<void()> _onDirty;

};

} // namespace Prime::E2E
