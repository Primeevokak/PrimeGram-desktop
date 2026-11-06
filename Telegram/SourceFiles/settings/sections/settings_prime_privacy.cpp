/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "settings/sections/settings_prime_privacy.h"

#include "core/application.h"
#include "main/main_domain.h"
#include "prime/prime_text.h"
#include "prime/privacy/privacy_passcode.h"
#include "settings/settings_common.h"
#include "storage/storage_domain.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

namespace Settings {
namespace {

using Prime::Text;
using Prime::TextValue;
namespace P = Prime::Privacy;

void ShowEmergencyBox(
		not_null<Window::SessionController*> controller,
		Fn<void()> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TextValue(
			"Emergency passcode",
			"Аварийный код-пароль"));
		const auto addField = [&](rpl::producer<QString> placeholder) {
			const auto wrap = box->addRow(
				object_ptr<Ui::RpWidget>(box),
				st::boxRowPadding);
			wrap->resize(wrap->width(), st::defaultInputField.heightMin);
			const auto field = Ui::CreateChild<Ui::PasswordInput>(
				wrap,
				st::defaultInputField,
				std::move(placeholder));
			wrap->widthValue(
			) | rpl::on_next([=](int width) {
				field->resize(width, field->height());
			}, wrap->lifetime());
			return not_null(field);
		};
		const auto first = addField(
			TextValue("Emergency passcode", "Аварийный код-пароль"));
		const auto second = addField(TextValue("Repeat", "Повторите"));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			TextValue(
				"Entering it on the lock screen erases all accounts and "
				"local data. Use something different from your passcode.",
				"Ввод на экране блокировки стирает все аккаунты и "
				"локальные данные. Используйте код, отличный от обычного."),
			st::boxLabel));
		const auto save = [=] {
			const auto text = first->getLastText();
			if (text.isEmpty()) {
				first->showError();
				return;
			} else if (text != second->getLastText()) {
				second->showError();
				return;
			}
			P::SetEmergencyPasscode(text);
			if (done) {
				done();
			}
			box->closeBox();
		};
		QObject::connect(first, &Ui::MaskedInputField::submitted, [=] {
			second->setFocus();
		});
		QObject::connect(second, &Ui::MaskedInputField::submitted, save);
		box->addButton(TextValue("Save", "Сохранить"), save);
		box->addButton(TextValue("Cancel", "Отмена"), [=] {
			box->closeBox();
		});
		if (P::EmergencyPasscodeSet()) {
			box->addLeftButton(TextValue("Remove", "Убрать"), [=] {
				P::ClearEmergencyPasscode();
				if (done) {
					done();
				}
				box->closeBox();
			});
		}
	}));
}

} // namespace

PrimePrivacy::PrimePrivacy(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> PrimePrivacy::title() {
	return TextValue("Privacy", "Приватность");
}

void PrimePrivacy::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);
	const auto state = content->lifetime().make_state<
		rpl::event_stream<>>();
	AddButtonWithLabel(
		content,
		TextValue("Emergency passcode", "Аварийный код-пароль"),
		rpl::single(rpl::empty) | rpl::then(
			state->events()
		) | rpl::map([] {
			return P::EmergencyPasscodeSet()
				? Text("On", "Включён")
				: Text("Off", "Выключен");
		}),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		if (!Core::App().domain().local().hasLocalPasscode()) {
			controller->window().showToast(Text(
				"Set a local passcode first: Privacy and Security",
				"Сначала задайте код-пароль: Конфиденциальность"));
			return;
		}
		ShowEmergencyBox(controller, [=] {
			state->fire({});
		});
	});
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"The emergency passcode works on the lock screen: instead of "
			"opening the app it logs out every account and removes local "
			"data, so the app looks freshly installed. After three wrong "
			"passcodes the lock screen makes you wait, five minutes more "
			"for each next mistake.",
			"Аварийный код работает на экране блокировки: вместо открытия "
			"приложения он выходит из всех аккаунтов и удаляет локальные "
			"данные, и приложение выглядит свежеустановленным. После трёх "
			"неверных паролей экран блокировки заставляет ждать, на пять "
			"минут больше за каждую следующую ошибку."));

	Ui::AddSkip(content);
	const auto winHelloEnabled = content->lifetime().make_state<
		rpl::variable<bool>>(Core::App().settings().systemUnlockEnabled());
	const auto winHello = content->add(object_ptr<Button>(
		content,
		TextValue(
			"Unlock with Windows Hello",
			"Разблокировка через Windows Hello"),
		st::settingsButtonNoIcon));
	winHello->toggleOn(winHelloEnabled->value());
	winHello->toggledValue(
	) | rpl::filter([=](bool toggled) {
		return toggled != Core::App().settings().systemUnlockEnabled();
	}) | rpl::on_next([=](bool toggled) {
		if (!Core::App().domain().local().hasLocalPasscode()) {
			controller->window().showToast(Text(
				"Set a local passcode first: Privacy and Security",
				"Сначала задайте код-пароль: Конфиденциальность"));
			*winHelloEnabled = false;
			return;
		}
		if (!toggled) {
			P::ClearStoredWindowsHelloPasscode();
		}
		Core::App().settings().setSystemUnlockEnabled(toggled);
		Core::App().saveSettingsDelayed();
		*winHelloEnabled = toggled;
	}, winHello->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"Use fingerprint, facial recognition or Windows PIN "
			"to quickly unlock PrimeGram without typing your passcode.",
			"Использовать отпечаток пальца, распознавание лица или PIN-код "
			"Windows Hello для быстрой разблокировки PrimeGram без ввода пароля."));

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
