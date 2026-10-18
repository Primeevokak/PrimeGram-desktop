/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "settings/sections/settings_prime_tgws.h"

#include "base/timer_rpl.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "prime/prime_text.h"
#include "prime/tgws/tgws_balancer.h"
#include "prime/tgws/tgws_manager.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_entity.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using Prime::Text;
using Prime::TextValue;
using Prime::TgWs::EngineState;

constexpr auto kStatusRefresh = 1000;
constexpr auto kProbeDc = 2;

[[nodiscard]] QString StatusText() {
	auto &manager = Core::App().tgws();
	if (!manager.enabled()) {
		return Text("Off", "Выключен");
	}
	auto &engine = manager.engine();
	switch (engine.state()) {
	case EngineState::Stopped:
		return Text("Stopped", "Остановлен");
	case EngineState::Starting:
		return Text("Starting…", "Запускается…");
	case EngineState::Failed:
		return Text("Error: ", "Ошибка: ") + engine.lastError();
	case EngineState::Running: {
		const auto stats = engine.stats();
		return Text("Running · %1 active", "Работает · активных: %1")
			.arg(stats.connectionsActive);
	}
	}
	return QString();
}

[[nodiscard]] rpl::producer<QString> StatusValue() {
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		Core::App().tgws().changes(),
		base::timer_each(kStatusRefresh)
	)) | rpl::map([] {
		return StatusText();
	}) | rpl::distinct_until_changed();
}

[[nodiscard]] QString PortLabel() {
	auto &manager = Core::App().tgws();
	const auto bound = manager.engine().boundPort();
	return QString::number(bound ? bound : manager.configuredPort());
}

[[nodiscard]] QString DomainLabel() {
	const auto pinned = Core::App().tgws().pinnedDomain();
	return pinned.isEmpty() ? Text("Auto", "Авто") : pinned;
}

[[nodiscard]] QString WorkersLabel() {
	const auto count = Core::App().tgws().workers().size();
	return count ? QString::number(count) : Text("None", "Нет");
}

[[nodiscard]] rpl::producer<QString> ChangesValue(
		Fn<QString()> make) {
	return rpl::single(rpl::empty) | rpl::then(
		Core::App().tgws().changes()
	) | rpl::map([=] {
		return make();
	});
}

void ShowPortBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TextValue("Local port", "Локальный порт"));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			TextValue("Port (1024–65535)", "Порт (1024–65535)"),
			QString::number(Core::App().tgws().configuredPort())));
		field->setInputMethodHints(Qt::ImhDigitsOnly);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			TextValue(
				"If the port is busy, the next free one is used.",
				"Если порт занят, будет выбран следующий свободный."),
			st::boxDividerLabel));
		const auto save = [=] {
			auto ok = false;
			const auto value = field->getLastText().trimmed().toInt(&ok);
			if (!ok || value < 1024 || value > 65535) {
				field->showError();
				return;
			}
			Core::App().tgws().setConfiguredPort(value);
			box->closeBox();
		};
		field->submits(
		) | rpl::on_next(save, field->lifetime());
		box->addButton(TextValue("Save", "Сохранить"), save);
		box->addButton(TextValue("Cancel", "Отмена"), [=] {
			box->closeBox();
		});
	}));
}

void ShowDomainBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TextValue("Connection domain", "Домен подключения"));
		box->addButton(TextValue("Close", "Закрыть"), [=] {
			box->closeBox();
		});
		const auto container = box->verticalLayout();
		auto &manager = Core::App().tgws();

		const auto add = [&](
				const QString &title,
				const QString &domain,
				bool measure) {
			const auto label
				= Ui::CreateChild<rpl::event_stream<QString>>(container.get());
			const auto pinned = (manager.pinnedDomain() == domain);
			const auto button = AddButtonWithLabel(
				container,
				rpl::single(title),
				label->events_starting_with(pinned
					? Text("selected", "выбран")
					: (measure ? Text("measuring…", "замер…") : QString())),
				st::settingsButtonNoIcon);
			button->addClickHandler([=] {
				Core::App().tgws().setPinnedDomain(domain);
				box->closeBox();
			});
			if (measure) {
				const auto guard = base::make_weak(box);
				Core::App().tgws().engine().probeDomain(
					domain,
					kProbeDc,
					[=](int ms) {
						crl::on_main(guard, [=] {
							label->fire_copy(ms < 0
								? Text("unreachable", "недоступен")
								: QString::number(ms) + u" ms"_q);
						});
					});
			}
		};
		add(Text("Automatic", "Автоматически"), QString(), false);
		auto domains = manager.engine().cfDomains();
		if (domains.isEmpty()) {
			domains = Prime::TgWs::DefaultCfDomains();
		}
		for (const auto &domain : domains) {
			add(domain, domain, true);
		}
	}));
}

void ShowWorkersBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TextValue("Cloudflare Workers", "Cloudflare Workers"));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			TextValue("Worker domains", "Домены воркеров"),
			Core::App().tgws().workers().join(QChar(' '))));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			TextValue(
				"Your own free tunnel on Cloudflare, used when the built-in "
				"domains do not answer. Separate several with spaces.",
				"Ваш собственный бесплатный туннель на Cloudflare — запасной "
				"путь, когда встроенные домены не отвечают. Несколько "
				"доменов — через пробел."),
			st::boxDividerLabel));
		const auto save = [=] {
			Core::App().tgws().setWorkers(
				Prime::TgWs::CoerceDomainList(field->getLastText()));
			box->closeBox();
		};
		field->submits(
		) | rpl::on_next(save, field->lifetime());
		box->addButton(TextValue("Save", "Сохранить"), save);
		box->addButton(TextValue("Cancel", "Отмена"), [=] {
			box->closeBox();
		});
	}));
}

void ShowLogBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(TextValue("TgWs log", "Журнал TgWs"));
		const auto lines = Core::App().tgws().engine().logLines();
		auto text = lines.isEmpty()
			? Text("The log is empty.", "Журнал пуст.")
			: lines.join(QChar('\n'));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(text),
			st::boxLabel));
		box->addButton(TextValue("Copy", "Скопировать"), [=] {
			TextUtilities::SetClipboardText({ text });
			controller->window().showToast(
				Text("Log copied", "Журнал скопирован"));
		});
		box->addButton(TextValue("Close", "Закрыть"), [=] {
			box->closeBox();
		});
	}));
}

} // namespace

PrimeTgWs::PrimeTgWs(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> PrimeTgWs::title() {
	return TextValue("PrimeGram TgWs", "PrimeGram TgWs");
}

void PrimeTgWs::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &manager = Core::App().tgws();

	Ui::AddSkip(content);
	const auto toggle = content->add(object_ptr<Button>(
		content,
		TextValue("Enable TgWs", "Включить TgWs"),
		st::settingsButtonNoIcon));
	toggle->toggleOn(rpl::single(manager.enabled()));
	toggle->toggledValue(
	) | rpl::filter([](bool toggled) {
		return toggled != Core::App().tgws().enabled();
	}) | rpl::on_next([](bool toggled) {
		Core::App().tgws().setEnabled(toggled);
	}, toggle->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"TgWs is built into the app: it reaches Telegram through "
			"WebSockets when direct access is blocked. It starts with "
			"the app and works through a local MTProto proxy.",
			"TgWs встроен в приложение: он подключается к Telegram через "
			"WebSocket, когда прямой доступ заблокирован. Запускается "
			"вместе с приложением и работает через локальный "
			"MTProto-прокси."));

	Ui::AddSkip(content);
	AddButtonWithLabel(
		content,
		TextValue("Status", "Состояние"),
		StatusValue(),
		st::settingsButtonNoIcon);
	Ui::AddSkip(content);

	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, TextValue("Connection", "Подключение"));
	AddButtonWithLabel(
		content,
		TextValue("Port", "Порт"),
		ChangesValue(PortLabel),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		ShowPortBox(controller);
	});
	AddButtonWithLabel(
		content,
		TextValue("Domain", "Домен"),
		ChangesValue(DomainLabel),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		ShowDomainBox(controller);
	});
	AddButtonWithLabel(
		content,
		TextValue("Cloudflare Workers", "Cloudflare Workers"),
		ChangesValue(WorkersLabel),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		ShowWorkersBox(controller);
	});
	Ui::AddSkip(content);

	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, TextValue("Diagnostics", "Диагностика"));
	AddButtonWithIcon(
		content,
		TextValue("Server log", "Журнал сервера"),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		ShowLogBox(controller);
	});
	AddButtonWithIcon(
		content,
		TextValue("Copy proxy link", "Скопировать ссылку на прокси"),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		TextUtilities::SetClipboardText({
			Core::App().tgws().engine().proxyLink()
		});
		controller->window().showToast(
			Text("Link copied", "Ссылка скопирована"));
	});
	Ui::AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
