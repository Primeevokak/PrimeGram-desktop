/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include <QtGui/QImage>

namespace Prime::NowPlaying {

struct Track {
	bool active = false;
	QString title;
	QString artist;
	QString album;
	QString app; // The player's application id, as Windows reports it.
	QImage cover;
	int durationSec = 0;
	int positionSec = 0;
	bool playing = false;

	[[nodiscard]] QString line() const {
		return artist.isEmpty() ? title : (title + u" — "_q + artist);
	}
};

// Reads what is playing right now in any player that tells Windows about it:
// Spotify, Yandex Music, a browser tab, a media player. The callback is
// called on the main thread; nothing playing gives an inactive track.
void FetchCurrentTrack(Fn<void(Track)> callback);

} // namespace Prime::NowPlaying
