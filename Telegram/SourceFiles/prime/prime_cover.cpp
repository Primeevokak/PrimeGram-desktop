/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/prime_cover.h"

#include "ui/ui_utility.h"

#include <QtGui/QPainter>

namespace Prime {

void PaintSpaceCover(QPainter &p, int width, int top, int height) {
	const auto bottom = top + height;
	if (bottom <= 0) {
		return;
	}
	static const auto image = QImage(u":/gui/art/prime_cover.png"_q);
	if (image.isNull()) {
		return;
	}
	const auto target = QRect(0, top, width, height);
	const auto source = QSizeF(width, height).scaled(
		image.size(),
		Qt::KeepAspectRatio);
	const auto sourceRect = QRectF(
		(image.width() - source.width()) / 2.,
		(image.height() - source.height()) / 2.,
		source.width(),
		source.height());
	p.save();
	p.setClipRect(QRect(0, 0, width, bottom));
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(target, image, sourceRect);
	p.restore();
}

} // namespace Prime
