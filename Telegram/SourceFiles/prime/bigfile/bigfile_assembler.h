/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "prime/bigfile/bigfile_format.h"

#include <rpl/event_stream.h>

#include <map>
#include <memory>

class HistoryItem;

namespace Api {
class MessagesSearch;
} // namespace Api

namespace Prime::BigFile {

// Puts a chunked file back together: finds all the parts in the chat,
// downloads them one by one and appends each to the result, deleting the
// part right away, so that only one part of extra space is ever needed.
class Assembler final : public base::has_weak_ptr {
public:
	Assembler();
	~Assembler();

	[[nodiscard]] static std::optional<Header> HeaderOf(
		not_null<HistoryItem*> item);

	void assemble(not_null<HistoryItem*> chunk);

	struct JobInfo {
		QString alias;
		QString fileName;
		int total = 0;
		int done = 0;
		float64 partProgress = 0.;
		bool searching = false;

		[[nodiscard]] float64 progress() const {
			return total ? ((done + partProgress) / total) : 0.;
		}
	};
	[[nodiscard]] std::vector<JobInfo> jobs() const;
	void cancel(const QString &alias);

	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

private:
	struct Job {
		QString key;
		QString alias;
		QString fileName;
		int64 totalSize = 0;
		int total = 0;
		uint64 userId = 0;
		uint64 peerId = 0;
		std::map<int, FullMsgId> chunks;
		int next = 1;
		QString destination;
		QString partPath;
		bool searching = false;
		bool downloading = false;
		int searchRounds = 0;
		float64 partProgress = 0.;
		std::unique_ptr<Api::MessagesSearch> search;
		rpl::lifetime searchLifetime;
	};

	[[nodiscard]] Job *find(const QString &key);
	[[nodiscard]] bool complete(const Job &job) const;
	void collectLoaded(Job &job);
	void startSearch(Job &job);
	void collectItem(Job &job, not_null<HistoryItem*> item);
	void proceed(const QString &key);
	void downloadNext(Job &job);
	void check();
	void partDownloaded(Job &job);
	void fail(const QString &key, const QString &text);
	void remove(const QString &key);
	void toast(const QString &text) const;

	std::vector<std::unique_ptr<Job>> _jobs;
	base::Timer _poll;
	rpl::event_stream<> _changes;

};

} // namespace Prime::BigFile
