/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "ui/abstract_button.h"

namespace Prime {

// A wide card with the space artwork that opens PrimeGram settings.
class Banner final : public Ui::AbstractButton {
public:
	explicit Banner(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	QString _title;
	QString _subtitle;

};

} // namespace Prime
