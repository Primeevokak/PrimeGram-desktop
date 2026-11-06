/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/flat_set.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "prime/e2e/e2e_store.h"

#include <rpl/event_stream.h>

#include <map>
#include <optional>
#include <vector>

class History;
class HistoryItem;
class PeerData;
class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Prime::E2E {

enum class State {
	Off,
	Requested, // We asked, the other side has not answered yet.
	IncomingPending, // The other side asked, the user has not answered.
	Established,
};

// End-to-end encryption of one-to-one chats: X3DH and the Double Ratchet over
// ordinary text messages, compatible with PrimeGram for Android.
class Manager final : public base::has_weak_ptr {
public:
	Manager();
	~Manager();

	void start();

	[[nodiscard]] bool eligible(not_null<PeerData*> peer) const;
	[[nodiscard]] State state(not_null<UserData*> user);

	void beginHandshake(not_null<UserData*> user);
	// Cancels a pending request, either ours or the other side's.
	void cancelHandshake(not_null<UserData*> user);
	void confirmIncoming(not_null<UserData*> user);
	// Tells the other side, then forgets the session.
	void disable(not_null<UserData*> user);
	// Forgets the session without telling anybody. For a session that fell
	// out of sync, when the other side cannot be reached or does not know.
	void reset(not_null<UserData*> user);
	// The right thing for a "reset" button in any state.
	void resetSmart(not_null<UserData*> user);

	// What the user typed to compare codes, and whether the codes matched.
	[[nodiscard]] QString seedPhrase(not_null<UserData*> user) const;
	void setSeedPhrase(not_null<UserData*> user, const QString &seed);
	[[nodiscard]] bool verified(not_null<UserData*> user) const;
	void setVerified(not_null<UserData*> user, bool value);

	// The six digit code that both people compute from the same seed phrase.
	[[nodiscard]] QString verificationCode(
		not_null<UserData*> user,
		const QString &seed);

	struct Outgoing {
		enum class Kind {
			Plain, // The chat is not encrypted: send as usual.
			Encrypted,
			Blocked, // Encrypted, but cannot send yet: never fall back.
		};
		Kind kind = Kind::Plain;
		QString wire;
	};
	// Called by ApiWrap::sendMessage for every outgoing text part.
	[[nodiscard]] Outgoing encryptOutgoing(
		not_null<History*> history,
		const QString &plain,
		FullMsgId localId);

	// Called by History::createItem for every created message.
	void processItem(not_null<HistoryItem*> item);
	// Called when a message of ours was just created locally.
	void processLocalItem(not_null<HistoryItem*> item);

	void wipeAll();

	[[nodiscard]] rpl::producer<not_null<UserData*>> stateChanges() const {
		return _stateChanges.events();
	}

private:
	struct Card {
		FullMsgId id;
		bool mine = false;
	};

	void hook(not_null<Main::Session*> session);
	void sendFramed(not_null<UserData*> user, const QString &text);
	void handleRequest(
		not_null<UserData*> user,
		const QString &raw,
		const Bundle &bundle,
		not_null<HistoryItem*> item);
	void handleResponse(not_null<UserData*> user, const Bundle &bundle);
	void handleCancel(not_null<UserData*> user);
	[[nodiscard]] QString decryptIncoming(
		not_null<UserData*> user,
		not_null<HistoryItem*> item,
		bool &failed);
	void notifyState(not_null<UserData*> user);
	void toast(const QString &text) const;

	// The in-chat card for a handshake request: text and buttons follow
	// the state of the session.
	void trackCard(not_null<UserData*> user, not_null<HistoryItem*> item);
	void paintCard(
		not_null<UserData*> user,
		not_null<HistoryItem*> item,
		bool mine);
	void refreshCards(not_null<UserData*> user);
	void showFailed(not_null<UserData*> user, not_null<HistoryItem*> item);

	[[nodiscard]] quint64 selfId(not_null<UserData*> user) const;
	[[nodiscard]] quint64 peerId(not_null<UserData*> user) const;
	[[nodiscard]] std::pair<quint64, quint64> key(
		not_null<UserData*> user) const;

	Store _store;
	base::flat_set<not_null<Main::Session*>> _hooked;
	// Plaintext of messages that are still being sent, by local id.
	std::map<FullMsgId, QString> _pending;
	// Plaintext for control messages, by the text that goes on the wire.
	std::map<QString, QString> _pendingByWire;
	std::map<std::pair<quint64, quint64>, std::vector<Card>> _cards;
	base::Timer _flush;
	base::Timer _flushSoon;
	rpl::event_stream<not_null<UserData*>> _stateChanges;
	rpl::lifetime _lifetime;
	bool _started = false;

};

} // namespace Prime::E2E
