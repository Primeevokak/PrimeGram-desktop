/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/prime_banner.h"

#include "prime/prime_text.h"
#include "ui/painter.h"
#include "styles/style_basic.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QPainterPath>

namespace Prime {
namespace {

constexpr auto kHeight = 92;
constexpr auto kMarginX = 14;
constexpr auto kMarginY = 8;
constexpr auto kRadius = 14.;
constexpr auto kLogo = 56;

} // namespace

Banner::Banner(QWidget *parent)
: AbstractButton(parent)
, _title(u"PrimeGram"_q)
, _subtitle(Text(
	"TgWs, big files, auto-delete and more",
	"TgWs, большие файлы, автоудаление и другое")) {
	setCursor(style::cur_pointer);
	setMouseTracking(true);
}

int Banner::resizeGetHeight(int newWidth) {
	return kHeight;
}

void Banner::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);

	const auto card = QRectF(
		kMarginX,
		kMarginY,
		width() - 2 * kMarginX,
		height() - 2 * kMarginY);
	auto clip = QPainterPath();
	clip.addRoundedRect(card, kRadius, kRadius);
	p.setClipPath(clip);

	static const auto art = QImage(u":/gui/art/prime_cover.png"_q);
	if (!art.isNull()) {
		const auto source = QSizeF(card.size()).scaled(
			art.size(),
			Qt::KeepAspectRatio);
		p.drawImage(
			card,
			art,
			QRectF(
				(art.width() - source.width()) / 2.,
				(art.height() - source.height()) / 2.,
				source.width(),
				source.height()));
	} else {
		p.fillRect(card, QColor(30, 20, 70));
	}
	auto shade = QLinearGradient(card.topLeft(), card.topRight());
	shade.setColorAt(0., QColor(6, 6, 24, 190));
	shade.setColorAt(0.65, QColor(6, 6, 24, 70));
	shade.setColorAt(1., QColor(6, 6, 24, 20));
	p.fillRect(card, shade);
	if (isOver() || isDown()) {
		p.fillRect(card, QColor(255, 255, 255, isDown() ? 34 : 18));
	}
	p.setClipping(false);

	p.setPen(QPen(QColor(170, 150, 255, 90), 1.));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

	static const auto logo = QImage(u":/gui/art/logo_256_no_margin.png"_q);
	const auto logoRect = QRectF(
		card.left() + 16,
		card.top() + (card.height() - kLogo) / 2.,
		kLogo,
		kLogo);
	if (!logo.isNull()) {
		p.drawImage(logoRect, logo);
	}

	const auto textLeft = int(logoRect.right()) + 14;
	const auto textWidth = int(card.right()) - 36 - textLeft;
	p.setPen(Qt::white);
	p.setFont(st::semiboldFont->f);
	p.drawText(
		QRect(textLeft, int(card.top()) + 18, textWidth, 24),
		Qt::AlignLeft | Qt::AlignVCenter,
		_title);
	p.setPen(QColor(205, 200, 235));
	p.setFont(st::normalFont->f);
	p.drawText(
		QRect(textLeft, int(card.top()) + 44, textWidth, 20),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::normalFont->elided(_subtitle, textWidth));

	auto chevron = QPainterPath();
	const auto cx = card.right() - 22;
	const auto cy = card.center().y();
	chevron.moveTo(cx - 3, cy - 7);
	chevron.lineTo(cx + 4, cy);
	chevron.lineTo(cx - 3, cy + 7);
	p.setPen(QPen(QColor(235, 230, 255), 2., Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	p.drawPath(chevron);
}

} // namespace Prime
