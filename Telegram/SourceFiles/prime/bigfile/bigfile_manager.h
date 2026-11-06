/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "prime/bigfile/bigfile_assembler.h"
#include "prime/bigfile/bigfile_format.h"

#include <rpl/event_stream.h>

class History;
class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Prime::BigFile {

// Sends a file that is too big for Telegram as a series of chunks, one at
// a time, waiting for each upload to finish. The plan survives restarts.
class Manager final : public base::has_weak_ptr {
public:
	Manager();
	~Manager();

	void start();

	[[nodiscard]] bool sendingEnabled() const;
	void setSendingEnabled(bool value);
	[[nodiscard]] bool experimental() const;
	void setExperimental(bool value);
	[[nodiscard]] int64 maxSendableSize() const;

	// Takes over a file that Telegram would refuse. Returns false when
	// the feature is off or the file is bigger than the ceiling.
	[[nodiscard]] bool trySend(
		not_null<PeerData*> peer,
		const QString &path);

	struct TransferInfo {
		QString alias;
		QString fileName;
		int64 totalSize = 0;
		int total = 0;
		int sent = 0;
		bool active = false;

		[[nodiscard]] float64 progress() const {
			return total ? (float64(sent) / total) : 0.;
		}
	};
	[[nodiscard]] std::vector<TransferInfo> transfers() const;
	void cancel(const QString &alias);

	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

	[[nodiscard]] Assembler &assembler() {
		return _assembler;
	}

private:
	struct Transfer {
		QString alias;
		QString sourcePath;
		QString fileName;
		int64 totalSize = 0;
		uint64 userId = 0;
		uint64 peerId = 0;
		int sent = 0;
		std::vector<Part> parts;
		int failures = 0;
	};

	void load();
	void save() const;
	[[nodiscard]] QString statePath() const;
	[[nodiscard]] QString chunkDirectory() const;

	void pump();
	void sendChunk(Transfer &transfer);
	void awaitChunk(const QString &alias, const QString &chunkName);
	void checkAwaited();
	void chunkFinished(bool success);
	void finish(const QString &alias);
	void scheduleNext(int delayMs);
	void clearChunkFile();

	[[nodiscard]] Main::Session *sessionFor(uint64 userId) const;
	[[nodiscard]] Transfer *find(const QString &alias);
	void toast(const QString &text) const;

	Assembler _assembler;
	std::vector<Transfer> _transfers;
	QString _currentAlias;
	QString _currentChunkPath;
	QString _currentChunkName;
	FullMsgId _currentMessage;
	crl::time _awaitingSince = 0;
	bool _busy = false;
	bool _started = false;

	base::Timer _poll;
	base::Timer _next;
	rpl::event_stream<> _changes;
	rpl::lifetime _watch;
	rpl::lifetime _lifetime;

};

} // namespace Prime::BigFile
