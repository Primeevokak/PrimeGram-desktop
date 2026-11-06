/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

class QPainter;

namespace Prime {

// Paints the space-themed welcome cover into (0, top, width, height).
void PaintSpaceCover(QPainter &p, int width, int top, int height);

} // namespace Prime
