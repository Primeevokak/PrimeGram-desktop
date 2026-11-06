/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/cache/prime_cache.h"

#include "data/data_session.h"
#include "data/data_types.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "prime/prime_text.h"
#include "storage/cache/storage_cache_database.h"
#include "storage/storage_account.h"
#include "ui/emoji_config.h"
#include "ui/text/format_values.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>
#include <rpl/variable.h>

namespace Prime::Cache {
namespace {

using Database = Storage::Cache::Database;

int64 MeasureDirectory(const QString &path) {
	if (path.isEmpty()) {
		return 0;
	}
	auto total = int64(0);
	auto it = QDirIterator(path, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		it.next();
		total += it.fileInfo().size();
	}
	return total;
}

void RemoveDirectoryContents(const QString &path) {
	if (path.isEmpty()) {
		return;
	}
	const auto dir = QDir(path);
	for (const auto &entry : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
		QFile::remove(entry.absoluteFilePath());
	}
	for (const auto &entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		QDir(entry.absoluteFilePath()).removeRecursively();
	}
}

} // namespace

QString FormatSize(int64 bytes) {
	return Ui::FormatSizeText(std::max(bytes, int64(0)));
}

QString KindTitle(Kind kind) {
	switch (kind) {
	case Kind::Images:
		return Prime::Text("Photos", "Фотографии");
	case Kind::VideosAndDocs:
		return Prime::Text("Videos and Files", "Видео и файлы");
	case Kind::StickersAndGifs:
		return Prime::Text("Stickers and GIFs", "Стикеры и GIF");
	case Kind::AudioAndVoice:
		return Prime::Text("Voice and Audio", "Голосовые и аудио");
	case Kind::TempFiles:
		return Prime::Text("Temporary Files", "Временные файлы");
	case Kind::Count:
		break;
	}
	return Prime::Text("Other", "Прочее");
}

void Measure(
		not_null<Main::Session*> session,
		Fn<void(CacheStats)> callback) {
	const auto tempDir = session->account().local().tempDirectory();
	auto &cache = session->data().cache();
	auto &bigCache = session->data().cacheBigFile();

	crl::async([tempDir, &cache, &bigCache, callback = std::move(callback)]() mutable {
		const auto tempSize = MeasureDirectory(tempDir);

		crl::on_main([&cache, &bigCache, tempSize, callback = std::move(callback)]() mutable {
			auto lifetime = std::make_shared<rpl::lifetime>();
			auto statsPtr = std::make_shared<std::optional<Database::Stats>>();
			auto statsBigPtr = std::make_shared<std::optional<Database::Stats>>();

			const auto checkDone = [lifetime, statsPtr, statsBigPtr, tempSize, callback]() {
				if (!*statsPtr || !*statsBigPtr) {
					return;
				}
				auto result = CacheStats();
				const auto &stats = **statsPtr;
				const auto &statsBig = **statsBigPtr;
				const auto getTagSize = [&](uint8 tag) -> int64 {
					const auto it = stats.tagged.find(tag);
					return (it != stats.tagged.end()) ? it->second.totalSize : 0;
				};

				result.images = getTagSize(Data::kImageCacheTag);
				result.stickersAndGifs = getTagSize(Data::kStickerCacheTag)
					+ getTagSize(Data::kAnimationCacheTag);
				result.audioAndVoice = getTagSize(Data::kVoiceMessageCacheTag)
					+ getTagSize(Data::kVideoMessageCacheTag);
				result.videosAndDocs = statsBig.full.totalSize;
				result.tempFiles = tempSize;

				lifetime->destroy();
				callback(result);
			};

			cache.statsOnMain() | rpl::on_next([statsPtr, checkDone](Database::Stats &&s) {
				*statsPtr = std::move(s);
				checkDone();
			}, *lifetime);

			bigCache.statsOnMain() | rpl::on_next([statsBigPtr, checkDone](Database::Stats &&s) {
				*statsBigPtr = std::move(s);
				checkDone();
			}, *lifetime);
		});
	});
}

void Clear(
		not_null<Main::Session*> session,
		Kind kind,
		Fn<void()> done) {
	auto &cache = session->data().cache();
	auto &bigCache = session->data().cacheBigFile();
	const auto tempDir = session->account().local().tempDirectory();

	switch (kind) {
	case Kind::Images:
		cache.clearByTag(Data::kImageCacheTag, [done = std::move(done)](auto) {
			if (done) done();
		});
		break;
	case Kind::StickersAndGifs:
		cache.clearByTag(Data::kStickerCacheTag);
		cache.clearByTag(Data::kAnimationCacheTag, [done = std::move(done)](auto) {
			Ui::Emoji::ClearIrrelevantCache();
			if (done) done();
		});
		break;
	case Kind::AudioAndVoice:
		cache.clearByTag(Data::kVoiceMessageCacheTag);
		cache.clearByTag(Data::kVideoMessageCacheTag, [done = std::move(done)](auto) {
			if (done) done();
		});
		break;
	case Kind::VideosAndDocs:
		bigCache.clear([done = std::move(done)](auto) {
			if (done) done();
		});
		break;
	case Kind::TempFiles:
		crl::async([tempDir, done = std::move(done)]() {
			RemoveDirectoryContents(tempDir);
			if (done) {
				crl::on_main([done = std::move(done)] {
					done();
				});
			}
		});
		break;
	case Kind::Count:
		if (done) done();
		break;
	}
}

void ClearAll(
		not_null<Main::Session*> session,
		Fn<void()> done) {
	auto &cache = session->data().cache();
	auto &bigCache = session->data().cacheBigFile();
	const auto tempDir = session->account().local().tempDirectory();

	cache.clear();
	bigCache.clear();
	Ui::Emoji::ClearIrrelevantCache();

	crl::async([tempDir, done = std::move(done)]() {
		RemoveDirectoryContents(tempDir);
		if (done) {
			crl::on_main([done = std::move(done)] {
				done();
			});
		}
	});
}

} // namespace Prime::Cache
