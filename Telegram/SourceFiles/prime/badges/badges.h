/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/timer.h"

#include <rpl/event_stream.h>

#include <unordered_map>

class PeerData;
class QNetworkAccessManager;

namespace Prime::Badges {

struct Badge {
	uint64 id = 0;
	bool chat = false;
	uint64 customEmojiId = 0;
	QString text;
};

// A badge next to a name, shown as a custom emoji: "supported PrimeGram",
// an official channel, anything the list wants to say. The list is one
// JSON file published on the project's docs site, refreshed every few hours.
class Manager final {
public:
	Manager();
	~Manager();

	void start();

	[[nodiscard]] const Badge *find(not_null<const PeerData*> peer) const;

	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

private:
	void refresh();
	void apply(const QByteArray &json, bool save);
	void notifyPeers();

	std::unordered_map<uint64, Badge> _users;
	std::unordered_map<uint64, Badge> _chats;
	std::unique_ptr<QNetworkAccessManager> _network;
	base::Timer _timer;
	rpl::event_stream<> _changes;

};

// Cheap helpers for the code that paints names.
[[nodiscard]] bool HasBadge(not_null<const PeerData*> peer);
[[nodiscard]] uint64 BadgeEmojiId(not_null<const PeerData*> peer);
[[nodiscard]] QString BadgeText(not_null<const PeerData*> peer);

} // namespace Prime::Badges
