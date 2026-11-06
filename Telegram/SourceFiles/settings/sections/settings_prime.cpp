/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "settings/sections/settings_prime.h"

#include "core/application.h"
#include "prime/autodelete/autodelete_manager.h"
#include "prime/bigfile/bigfile_manager.h"
#include "prime/prime_hub.h"
#include "prime/prime_text.h"
#include "settings/sections/settings_prime_interface.h"
#include "settings/sections/settings_prime_privacy.h"
#include "settings/sections/settings_prime_storage.h"
#include "settings/sections/settings_prime_tgws.h"
#include "settings/sections/settings_prime_voice.h"
#include "prime/updates/prime_update_checker.h"
#include "prime/mcp/prime_mcp_server.h"
#include "core/version.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Settings {
namespace {

using Prime::Text;
using Prime::TextValue;

[[nodiscard]] QString TransferLabel(
		const Prime::BigFile::Manager::TransferInfo &info) {
	return QString::number(info.sent)
		+ '/'
		+ QString::number(info.total)
		+ u" · "_q
		+ QString::number(int(info.progress() * 100))
		+ '%';
}

} // namespace

PrimeMain::PrimeMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> PrimeMain::title() {
	return TextValue("PrimeGram", "PrimeGram");
}

void PrimeMain::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);
	AddButtonWithIcon(
		content,
		TextValue("TgWs connection", "Подключение TgWs"),
		st::settingsButton,
		{ &st::menuIconNetwork }
	)->addClickHandler([=] {
		showOther(PrimeTgWs::Id());
	});
	AddButtonWithIcon(
		content,
		TextValue("Big files", "Большие файлы"),
		st::settingsButton,
		{ &st::menuIconDownload }
	)->addClickHandler([=] {
		showOther(PrimeBigFile::Id());
	});
	AddButtonWithIcon(
		content,
		TextValue("Voice recognition", "Распознавание голоса"),
		st::settingsButton,
		{ &st::menuIconUnmute }
	)->addClickHandler([=] {
		showOther(PrimeVoice::Id());
	});
	AddButtonWithIcon(
		content,
		TextValue("Interface and chats", "Интерфейс и чаты"),
		st::settingsButton,
		{ &st::menuIconChats }
	)->addClickHandler([=] {
		showOther(PrimeInterface::Id());
	});
	AddButtonWithIcon(
		content,
		TextValue("Privacy", "Приватность"),
		st::settingsButton,
		{ &st::menuIconLock }
	)->addClickHandler([=] {
		showOther(PrimePrivacy::Id());
	});
	AddButtonWithIcon(
		content,
		TextValue("Auto-delete messages", "Автоудаление сообщений"),
		st::settingsButton,
		{ &st::menuIconTTL }
	)->addClickHandler([=] {
		showOther(PrimeAutoDelete::Id());
	});
	AddButtonWithIcon(
		content,
		TextValue("Storage and cache", "Память и кэш"),
		st::settingsButton,
		{ &st::menuIconStorage }
	)->addClickHandler([=] {
		showOther(PrimeStorage::Id());
	});
	AddButtonWithIcon(
		content,
		TextValue("MCP Server (AI Integration)", "MCP Сервер (ИИ Интеграция)"),
		st::settingsButton,
		{ &st::menuIconNetwork }
	)->addClickHandler([=] {
		showOther(PrimeMcp::Id());
	});
	AddButtonWithLabel(
		content,
		TextValue("Check for updates", "Проверить обновления"),
		rpl::single(QString("v%1").arg(AppVersionStr)),
		st::settingsButton,
		{ &st::menuIconDownload }
	)->addClickHandler([=] {
		Prime::Updates::Check(true);
	});
	Ui::AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

PrimeBigFile::PrimeBigFile(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> PrimeBigFile::title() {
	return TextValue("Big files", "Большие файлы");
}

void PrimeBigFile::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &manager = Core::App().bigFile();

	Ui::AddSkip(content);
	const auto sending = content->add(object_ptr<Button>(
		content,
		TextValue(
			"Send files over the Telegram limit",
			"Отправлять файлы больше лимита Telegram"),
		st::settingsButtonNoIcon));
	sending->toggleOn(rpl::single(manager.sendingEnabled()));
	sending->toggledValue(
	) | rpl::filter([](bool toggled) {
		return toggled != Core::App().bigFile().sendingEnabled();
	}) | rpl::on_next([](bool toggled) {
		Core::App().bigFile().setSendingEnabled(toggled);
	}, sending->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"A file over 2 GB (4 GB with Premium) is sent as several "
			"parts, up to 8 GB in total. Another PrimeGram assembles "
			"them back into one file.",
			"Файл больше 2 ГБ (4 ГБ с Premium) отправляется несколькими "
			"частями, всего до 8 ГБ. Другой PrimeGram собирает их обратно "
			"в один файл."));

	Ui::AddSkip(content);
	const auto experimental = content->add(object_ptr<Button>(
		content,
		TextValue(
			"Experimental: up to 50 GB",
			"Экспериментально: до 50 ГБ"),
		st::settingsButtonNoIcon));
	experimental->toggleOn(rpl::single(manager.experimental()));
	experimental->toggledValue(
	) | rpl::filter([](bool toggled) {
		return toggled != Core::App().bigFile().experimental();
	}) | rpl::on_next([](bool toggled) {
		Core::App().bigFile().setExperimental(toggled);
	}, experimental->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"Telegram may limit accounts that send very large amounts "
			"of data.",
			"Telegram может ограничить аккаунты, которые отправляют очень "
			"большие объёмы данных."));

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(
		content,
		TextValue("Sending now", "Сейчас отправляется"));
	const auto list = content->add(object_ptr<Ui::VerticalLayout>(content));
	const auto rebuild = [=] {
		list->clear();
		for (const auto &info : Core::App().bigFile().transfers()) {
			const auto alias = info.alias;
			AddButtonWithLabel(
				list,
				rpl::single(info.fileName),
				rpl::single(TransferLabel(info)),
				st::settingsButtonNoIcon
			)->addClickHandler([=] {
				controller->show(Ui::MakeConfirmBox({
					.text = TextValue(
						"Stop sending this file?",
						"Остановить отправку этого файла?"),
					.confirmed = [=](Fn<void()> close) {
						Core::App().bigFile().cancel(alias);
						close();
					},
					.confirmText = TextValue("Stop", "Остановить"),
				}));
			});
		}
		if (!list->count()) {
			Ui::AddDividerText(
				list,
				TextValue("Nothing is being sent.", "Ничего не отправляется."));
		}
	};
	rebuild();
	manager.changes(
	) | rpl::on_next(rebuild, content->lifetime());
	Ui::AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

PrimeAutoDelete::PrimeAutoDelete(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> PrimeAutoDelete::title() {
	return TextValue("Auto-delete messages", "Автоудаление сообщений");
}

void PrimeAutoDelete::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &manager = Core::App().prime().autoDelete();

	Ui::AddSkip(content);
	const auto toggle = content->add(object_ptr<Button>(
		content,
		TextValue(
			"Delete my messages automatically",
			"Автоматически удалять мои сообщения"),
		st::settingsButtonNoIcon));
	toggle->toggleOn(rpl::single(manager.enabled()));
	toggle->toggledValue(
	) | rpl::filter([](bool toggled) {
		return toggled != Core::App().prime().autoDelete().enabled();
	}) | rpl::on_next([](bool toggled) {
		Core::App().prime().autoDelete().setEnabled(toggled);
	}, toggle->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"Your own messages disappear for everyone after the chosen "
			"time. A minute before, a text message is replaced with a "
			"dot. It can be turned on or off for a single chat from its "
			"menu. Works while PrimeGram is running.",
			"Ваши сообщения исчезают у всех через выбранное время. За "
			"минуту до этого текстовое сообщение заменяется точкой. "
			"Для отдельного чата включается и выключается в его меню. "
			"Работает, пока запущен PrimeGram."));

	Ui::AddSkip(content);
	AddButtonWithLabel(
		content,
		TextValue("Delete after", "Удалять через"),
		rpl::single(rpl::empty) | rpl::then(
			Core::App().prime().autoDelete().changes()
		) | rpl::map([] {
			const auto hours = Core::App().prime().autoDelete().periodHours();
			return (hours % 24 == 0)
				? Text("%1 d", "%1 д").arg(hours / 24)
				: Text("%1 h", "%1 ч").arg(hours);
		}),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(TextValue("Delete after", "Удалять через"));
			for (const auto hours : { 24, 48, 72, 120, 168 }) {
				AddButtonWithIcon(
					box->verticalLayout(),
					rpl::single(Text("%1 d", "%1 д").arg(hours / 24)),
					st::settingsButtonNoIcon
				)->addClickHandler([=] {
					Core::App().prime().autoDelete().setPeriodHours(hours);
					box->closeBox();
				});
			}
			box->addButton(TextValue("Close", "Закрыть"), [=] {
				box->closeBox();
			});
		}));
	});
	Ui::AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

PrimeMcp::PrimeMcp(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> PrimeMcp::title() {
	return TextValue("MCP Server (AI)", "MCP Сервер (ИИ)");
}

void PrimeMcp::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &server = Prime::Mcp::Server::Instance();

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(
		content,
		TextValue("Model Context Protocol", "Model Context Protocol"));

	const auto serverToggle = content->add(object_ptr<Button>(
		content,
		TextValue(
			"Enable MCP Server (127.0.0.1:28888)",
			"Включить MCP Сервер (127.0.0.1:28888)"),
		st::settingsButtonNoIcon));
	serverToggle->toggleOn(rpl::single(server.enabled()));
	serverToggle->toggledValue(
	) | rpl::on_next([&server](bool enabled) {
		server.setEnabled(enabled);
	}, serverToggle->lifetime());

	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"Local MCP server allows AI assistants (Cursor, Claude, Antigravity) to access chats and search messages in Telegram.",
			"Локальный MCP-сервер позволяет ИИ-ассистентам (Cursor, Claude, Antigravity) читать чаты и искать сообщения в Telegram."));

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(
		content,
		TextValue("Permissions", "Разрешения доступа"));

	const auto sendToggle = content->add(object_ptr<Button>(
		content,
		TextValue(
			"Allow AI to send messages",
			"Разрешить отправку сообщений через ИИ"),
		st::settingsButtonNoIcon));
	sendToggle->toggleOn(rpl::single(server.allowSend()));
	sendToggle->toggledValue(
	) | rpl::on_next([&server](bool allow) {
		server.setAllowSend(allow);
	}, sendToggle->lifetime());

	const auto deleteToggle = content->add(object_ptr<Button>(
		content,
		TextValue(
			"Allow AI to delete messages",
			"Разрешить удаление сообщений через ИИ"),
		st::settingsButtonNoIcon));
	deleteToggle->toggleOn(rpl::single(server.allowDelete()));
	deleteToggle->toggledValue(
	) | rpl::on_next([&server](bool allow) {
		server.setAllowDelete(allow);
	}, deleteToggle->lifetime());

	Ui::AddSkip(content);
	AddButtonWithLabel(
		content,
		TextValue("Copy MCP Configuration", "Скопировать конфиг MCP"),
		rpl::single(QString("JSON")),
		st::settingsButton,
		{ &st::menuIconDownload }
	)->addClickHandler([&server] {
		QGuiApplication::clipboard()->setText(server.configJson());
		Ui::Toast::Show(Text(
			"MCP configuration copied to clipboard!",
			"Конфигурация MCP скопирована в буфер обмена!"));
	});

	Ui::AddSkip(content);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
