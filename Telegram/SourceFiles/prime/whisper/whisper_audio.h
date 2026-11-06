/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include <atomic>
#include <vector>

namespace Prime::Whisper {

// Decodes any audio or video file into mono 16 kHz float samples in
// [-1, 1], which is what whisper expects. Empty on failure.
[[nodiscard]] std::vector<float> DecodeToPcm16k(
	const QString &path,
	const QByteArray &bytes,
	const std::atomic<bool> *cancel = nullptr);

} // namespace Prime::Whisper
