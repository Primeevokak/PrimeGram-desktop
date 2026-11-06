/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/nowplaying/nowplaying_track.h"

namespace Prime::NowPlaying {

// A wide picture with the cover, the title, the artist and the progress.
[[nodiscard]] QImage RenderCard(const Track &track);

} // namespace Prime::NowPlaying
