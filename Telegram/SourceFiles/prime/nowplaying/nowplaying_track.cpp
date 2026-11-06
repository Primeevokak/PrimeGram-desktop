/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/nowplaying/nowplaying_track.h"

#ifdef Q_OS_WIN
#include "base/platform/win/base_windows_winrt.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#endif // Q_OS_WIN

namespace Prime::NowPlaying {
namespace {

#ifdef Q_OS_WIN

[[nodiscard]] QString ToQString(const winrt::hstring &value) {
	return QString::fromWCharArray(value.c_str(), int(value.size()));
}

[[nodiscard]] QImage ReadThumbnail(
		const winrt::Windows::Storage::Streams::IRandomAccessStreamReference
			&reference) {
	using namespace winrt::Windows::Storage::Streams;
	if (!reference) {
		return {};
	}
	const auto stream = reference.OpenReadAsync().get();
	const auto size = uint32_t(stream.Size());
	if (!size || size > 16 * 1024 * 1024) {
		return {};
	}
	const auto reader = DataReader(stream);
	reader.LoadAsync(size).get();
	auto bytes = QByteArray(int(size), Qt::Uninitialized);
	reader.ReadBytes(winrt::array_view<uint8_t>(
		reinterpret_cast<uint8_t*>(bytes.data()),
		reinterpret_cast<uint8_t*>(bytes.data()) + bytes.size()));
	return QImage::fromData(bytes);
}

[[nodiscard]] Track ReadFromWindows() {
	using namespace winrt::Windows::Media::Control;
	using Status = GlobalSystemMediaTransportControlsSessionPlaybackStatus;

	auto result = Track();
	// This is a worker thread: it needs its own COM apartment.
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
	} catch (...) {
	}
	const auto read = base::WinRT::Try([&] {
		const auto manager
			= GlobalSystemMediaTransportControlsSessionManager::RequestAsync()
				.get();
		if (!manager) {
			return;
		}
		auto chosen = GlobalSystemMediaTransportControlsSession(nullptr);
		auto chosenPlaying = false;
		const auto consider = [&](
				const GlobalSystemMediaTransportControlsSession &session) {
			if (!session) {
				return;
			}
			const auto playing = (session.GetPlaybackInfo().PlaybackStatus()
				== Status::Playing);
			if (!chosen || (playing && !chosenPlaying)) {
				chosen = session;
				chosenPlaying = playing;
			}
		};
		consider(manager.GetCurrentSession());
		for (const auto &session : manager.GetSessions()) {
			consider(session);
		}
		if (!chosen) {
			return;
		}
		const auto properties = chosen.TryGetMediaPropertiesAsync().get();
		if (!properties) {
			return;
		}
		result.title = ToQString(properties.Title());
		result.artist = ToQString(properties.Artist());
		result.album = ToQString(properties.AlbumTitle());
		result.app = ToQString(chosen.SourceAppUserModelId());
		result.playing = chosenPlaying;
		const auto timeline = chosen.GetTimelineProperties();
		const auto toSeconds = [](auto span) {
			return int(std::chrono::duration_cast<std::chrono::seconds>(
				span).count());
		};
		result.durationSec = std::max(
			0,
			toSeconds(timeline.EndTime()) - toSeconds(timeline.StartTime()));
		result.positionSec = std::clamp(
			toSeconds(timeline.Position()),
			0,
			result.durationSec ? result.durationSec : 1 << 30);
		const auto thumbnail = base::WinRT::TryNoCheck([&] {
			return ReadThumbnail(properties.Thumbnail());
		});
		if (thumbnail) {
			result.cover = *thumbnail;
		}
		result.active = !result.title.isEmpty();
	});
	if (!read) {
		return Track();
	}
	return result;
}

#endif // Q_OS_WIN

} // namespace

void FetchCurrentTrack(Fn<void(Track)> callback) {
#ifdef Q_OS_WIN
	crl::async([=] {
		auto track = ReadFromWindows();
		crl::on_main([=, track = std::move(track)]() mutable {
			callback(std::move(track));
		});
	});
#else // Q_OS_WIN
	callback(Track());
#endif // Q_OS_WIN
}

} // namespace Prime::NowPlaying
