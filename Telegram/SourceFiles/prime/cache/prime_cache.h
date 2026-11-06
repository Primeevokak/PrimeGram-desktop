/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/basic_types.h"
#include <QtCore/QString>

namespace Main {
class Session;
} // namespace Main

namespace Prime::Cache {

enum class Kind {
	Images,
	VideosAndDocs,
	StickersAndGifs,
	AudioAndVoice,
	TempFiles,
	Count
};

struct CacheStats {
	int64 images = 0;
	int64 videosAndDocs = 0;
	int64 stickersAndGifs = 0;
	int64 audioAndVoice = 0;
	int64 tempFiles = 0;

	[[nodiscard]] int64 total() const {
		return images + videosAndDocs + stickersAndGifs + audioAndVoice + tempFiles;
	}
};

[[nodiscard]] QString FormatSize(int64 bytes);
[[nodiscard]] QString KindTitle(Kind kind);

// Async measurement of cache sizes per kind.
void Measure(
	not_null<Main::Session*> session,
	Fn<void(CacheStats)> callback);

// Clear specific kind.
void Clear(
	not_null<Main::Session*> session,
	Kind kind,
	Fn<void()> done);

// Clear all cache.
void ClearAll(
	not_null<Main::Session*> session,
	Fn<void()> done);

} // namespace Prime::Cache
