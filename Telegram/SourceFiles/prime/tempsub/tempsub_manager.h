/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/timer.h"

#include <rpl/event_stream.h>

class PeerData;

namespace Prime::TempSub {

// A channel or a group joined for a limited time: left automatically.
class Manager final {
public:
	Manager();

	void start();

	struct Entry {
		uint64 userId = 0;
		uint64 peerId = 0;
		int64 expiresAt = 0; // Unix seconds.
		QString title;
	};

	void schedule(not_null<PeerData*> peer, int64 expiresAt);
	void cancel(not_null<PeerData*> peer);
	[[nodiscard]] const Entry *entryFor(not_null<PeerData*> peer) const;
	[[nodiscard]] const std::vector<Entry> &all() const {
		return _entries;
	}

	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

private:
	void sweep();
	void load();
	void save() const;

	std::vector<Entry> _entries;
	base::Timer _timer;
	rpl::event_stream<> _changes;
	bool _started = false;

};

} // namespace Prime::TempSub
