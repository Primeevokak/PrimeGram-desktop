/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/nowplaying/nowplaying_card.h"

#include <QtGui/QFont>
#include <QtGui/QFontMetrics>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

namespace Prime::NowPlaying {
namespace {

constexpr auto kWidth = 1000;
constexpr auto kHeight = 360;
constexpr auto kCover = 280;
constexpr auto kPadding = 40;

[[nodiscard]] QString Time(int seconds) {
	return QString::number(seconds / 60)
		+ ':'
		+ QString::number(seconds % 60).rightJustified(2, '0');
}

[[nodiscard]] QColor Average(const QImage &image) {
	if (image.isNull()) {
		return QColor(60, 40, 120);
	}
	const auto small = image.scaled(
		1,
		1,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
	return QColor(small.pixel(0, 0));
}

// A cheap blur: shrink a lot and stretch back with smoothing.
[[nodiscard]] QImage Blurred(const QImage &image) {
	const auto tiny = image.scaled(
		24,
		24,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	return tiny.scaled(
		kWidth,
		kHeight,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
}

} // namespace

QImage RenderCard(const Track &track) {
	auto result = QImage(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.setRenderHint(QPainter::TextAntialiasing);

	auto clip = QPainterPath();
	clip.addRect(QRectF(0, 0, kWidth, kHeight));
	p.setClipPath(clip);

	const auto base = Average(track.cover);
	if (!track.cover.isNull()) {
		const auto blurred = Blurred(track.cover);
		p.drawImage(
			QRect(0, 0, kWidth, kHeight),
			blurred,
			QRect(
				(blurred.width() - kWidth) / 2,
				(blurred.height() - kHeight) / 2,
				kWidth,
				kHeight));
	} else {
		auto gradient = QLinearGradient(0, 0, kWidth, kHeight);
		gradient.setColorAt(0., base.darker(110));
		gradient.setColorAt(1., QColor(20, 18, 48));
		p.fillRect(QRect(0, 0, kWidth, kHeight), gradient);
	}
	auto shade = QLinearGradient(0, 0, kWidth, 0);
	shade.setColorAt(0., QColor(0, 0, 0, 110));
	shade.setColorAt(1., QColor(0, 0, 0, 170));
	p.fillRect(QRect(0, 0, kWidth, kHeight), shade);

	const auto coverRect = QRectF(
		kPadding,
		(kHeight - kCover) / 2.,
		kCover,
		kCover);
	auto coverClip = QPainterPath();
	coverClip.addRoundedRect(coverRect, 26, 26);
	p.save();
	p.setClipPath(coverClip);
	if (!track.cover.isNull()) {
		const auto source = QSizeF(coverRect.size()).scaled(
			track.cover.size(),
			Qt::KeepAspectRatio);
		p.drawImage(
			coverRect,
			track.cover,
			QRectF(
				(track.cover.width() - source.width()) / 2.,
				(track.cover.height() - source.height()) / 2.,
				source.width(),
				source.height()));
	} else {
		auto gradient = QLinearGradient(
			coverRect.topLeft(),
			coverRect.bottomRight());
		gradient.setColorAt(0., QColor(120, 90, 230));
		gradient.setColorAt(1., QColor(40, 30, 110));
		p.fillRect(coverRect, gradient);
		auto font = QFont(u"Segoe UI"_q);
		font.setPixelSize(150);
		p.setFont(font);
		p.setPen(QColor(255, 255, 255, 220));
		p.drawText(coverRect, Qt::AlignCenter, QString::fromUtf8("♪"));
	}
	p.restore();

	const auto left = kPadding + kCover + 40;
	const auto width = kWidth - left - kPadding;
	auto title = QFont(u"Segoe UI"_q);
	title.setPixelSize(48);
	title.setBold(true);
	auto artist = QFont(u"Segoe UI"_q);
	artist.setPixelSize(34);
	auto album = QFont(u"Segoe UI"_q);
	album.setPixelSize(27);

	const auto hasProgress = track.durationSec > 0;
	auto top = hasProgress ? 62 : 90;

	p.setFont(title);
	p.setPen(Qt::white);
	p.drawText(
		QRect(left, top, width, 64),
		Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetrics(title).elidedText(
			track.title,
			Qt::ElideRight,
			width));
	top += 70;
	if (!track.artist.isEmpty()) {
		p.setFont(artist);
		p.setPen(QColor(240, 238, 255, 235));
		p.drawText(
			QRect(left, top, width, 48),
			Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(artist).elidedText(
				track.artist,
				Qt::ElideRight,
				width));
		top += 52;
	}
	if (!track.album.isEmpty()) {
		p.setFont(album);
		p.setPen(QColor(220, 215, 245, 170));
		p.drawText(
			QRect(left, top, width, 40),
			Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(album).elidedText(
				track.album,
				Qt::ElideRight,
				width));
	}

	if (hasProgress) {
		const auto barTop = kHeight - 96;
		const auto barRect = QRectF(left, barTop, width, 8);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 70));
		p.drawRoundedRect(barRect, 4, 4);
		const auto fraction = std::clamp(
			double(track.positionSec) / track.durationSec,
			0.,
			1.);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			QRectF(barRect.left(), barRect.top(), barRect.width() * fraction, 8),
			4,
			4);
		auto small = QFont(u"Segoe UI"_q);
		small.setPixelSize(24);
		p.setFont(small);
		p.setPen(QColor(235, 232, 255, 190));
		p.drawText(
			QRect(left, barTop + 16, width / 2, 32),
			Qt::AlignLeft | Qt::AlignVCenter,
			Time(track.positionSec));
		p.drawText(
			QRect(left + width / 2, barTop + 16, width / 2, 32),
			Qt::AlignRight | Qt::AlignVCenter,
			Time(track.durationSec));
	}
	return result;
}

} // namespace Prime::NowPlaying
