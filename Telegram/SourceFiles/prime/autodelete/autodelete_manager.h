/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/flat_set.h"
#include "base/timer.h"
#include "base/weak_ptr.h"

#include <rpl/event_stream.h>

class PeerData;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Prime::AutoDelete {

// Deletes the user's own outgoing messages a fixed time after sending.
// The schedule is kept on disk, the sweep runs once in half a minute.
class Manager final : public base::has_weak_ptr {
public:
	Manager();
	~Manager();

	void start();

	[[nodiscard]] bool enabled() const;
	void setEnabled(bool value);

	[[nodiscard]] int periodHours() const;
	void setPeriodHours(int hours);

	// Per chat: 1 on, 0 off, -1 follow the global switch.
	[[nodiscard]] int chatOverride(not_null<PeerData*> peer) const;
	void setChatOverride(not_null<PeerData*> peer, int value);
	[[nodiscard]] bool enabledFor(not_null<PeerData*> peer) const;

	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

private:
	struct Entry {
		uint64 userId = 0;
		uint64 peerId = 0;
		int64 msgId = 0;
		int64 editAt = 0;
		int64 deleteAt = 0;
		bool edited = false;
	};

	void hook(not_null<Main::Session*> session);
	void onSent(not_null<HistoryItem*> item);
	void sweep();
	void load();
	void save() const;
	[[nodiscard]] QString chatKey(not_null<PeerData*> peer) const;

	std::vector<Entry> _entries;
	base::flat_set<not_null<Main::Session*>> _hooked;
	QHash<QString, int> _overrides;
	base::Timer _timer;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;
	bool _started = false;

};

} // namespace Prime::AutoDelete
