/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "settings/sections/settings_prime_voice.h"

#include "core/application.h"
#include "prime/prime_hub.h"
#include "prime/prime_text.h"
#include "prime/whisper/whisper_manager.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QThread>

namespace Settings {
namespace {

using Prime::Text;
using Prime::TextValue;
namespace W = Prime::Whisper;

struct Language {
	const char *code;
	const char *en;
	const char *ru;
};

constexpr Language kLanguages[] = {
	{ "", "Auto-detect", "Определять автоматически" },
	{ "ru", "Russian", "Русский" },
	{ "en", "English", "Английский" },
	{ "uk", "Ukrainian", "Украинский" },
	{ "be", "Belarusian", "Белорусский" },
	{ "de", "German", "Немецкий" },
	{ "fr", "French", "Французский" },
	{ "es", "Spanish", "Испанский" },
	{ "it", "Italian", "Итальянский" },
	{ "pt", "Portuguese", "Португальский" },
	{ "tr", "Turkish", "Турецкий" },
	{ "pl", "Polish", "Польский" },
	{ "ar", "Arabic", "Арабский" },
	{ "zh", "Chinese", "Китайский" },
	{ "ja", "Japanese", "Японский" },
	{ "ko", "Korean", "Корейский" },
};

[[nodiscard]] QString LanguageName(const QString &code) {
	for (const auto &language : kLanguages) {
		if (code == QLatin1String(language.code)) {
			return Text(language.en, language.ru);
		}
	}
	return code;
}

[[nodiscard]] QString ModelStatus(int index) {
	auto &manager = Core::App().prime().whisper();
	auto &downloader = manager.downloader();
	if (downloader.current() == index) {
		return QString::number(int(downloader.progress() * 100)) + '%';
	} else if (W::IsInstalled(index)) {
		return (manager.model() == index)
			? Text("In use", "Используется")
			: Text("Installed", "Установлена");
	}
	return Ui::FormatSizeText(W::Model(index).bytes);
}

void ShowModelBox(
		not_null<Window::SessionController*> controller,
		int index) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto &info = W::Model(index);
		box->setTitle(rpl::single(QString::fromUtf8(info.title)));
		const auto container = box->verticalLayout();
		auto &manager = Core::App().prime().whisper();
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(Text(info.noteEn, info.noteRu)),
				st::boxLabel),
			st::boxRowPadding);
		if (info.heavy) {
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					TextValue(
						"Needs a lot of memory and a fast processor.",
						"Нужно много памяти и быстрый процессор."),
					st::boxLabel),
				st::boxRowPadding);
		}
		const auto add = [=](const QString &text, Fn<void()> handler) {
			AddButtonWithIcon(
				container,
				rpl::single(text),
				st::settingsButtonNoIcon
			)->addClickHandler([=] {
				handler();
				box->closeBox();
			});
		};
		auto &downloader = manager.downloader();
		if (downloader.current() == index) {
			add(Text("Cancel download", "Отменить загрузку"), [=] {
				Core::App().prime().whisper().downloader().cancel();
			});
		} else if (W::IsInstalled(index)) {
			if (manager.model() != index) {
				add(Text("Use this model", "Использовать эту модель"), [=] {
					Core::App().prime().whisper().setModel(index);
				});
			}
			add(Text("Delete from disk", "Удалить с диска"), [=] {
				W::Remove(index);
				Core::App().prime().whisper().setEnabled(
					Core::App().prime().whisper().enabled());
			});
		} else if (!downloader.active()) {
			add(Text("Download", "Скачать"), [=] {
				auto &manager = Core::App().prime().whisper();
				manager.downloader().start(index);
				if (manager.model() != index
					&& !W::IsInstalled(manager.model())) {
					manager.setModel(index);
				}
			});
		}
		box->addButton(TextValue("Close", "Закрыть"), [=] {
			box->closeBox();
		});
	}));
}

} // namespace

PrimeVoice::PrimeVoice(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> PrimeVoice::title() {
	return TextValue("Voice recognition", "Распознавание голоса");
}

void PrimeVoice::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto &manager = Core::App().prime().whisper();

	Ui::AddSkip(content);
	const auto toggle = content->add(object_ptr<Button>(
		content,
		TextValue(
			"Recognize voice messages on this computer",
			"Распознавать голосовые на этом компьютере"),
		st::settingsButtonNoIcon));
	toggle->toggleOn(rpl::single(manager.enabled()));
	toggle->toggledValue(
	) | rpl::filter([](bool toggled) {
		return toggled != Core::App().prime().whisper().enabled();
	}) | rpl::on_next([](bool toggled) {
		Core::App().prime().whisper().setEnabled(toggled);
	}, toggle->lifetime());
	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"Works without Telegram Premium and without sending the "
			"recording anywhere: a model on your disk does everything. "
			"The text appears under the voice message when you press the "
			"recognition button. A model is downloaded once.",
			"Работает без Telegram Premium и ничего никуда не отправляет: "
			"всё делает модель на вашем диске. Текст появляется под "
			"голосовым сообщением по кнопке распознавания. Модель "
			"скачивается один раз."));

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, TextValue("Model", "Модель"));
	const auto status = [=](int index) {
		return rpl::single(rpl::empty) | rpl::then(rpl::merge(
			Core::App().prime().whisper().changes(),
			Core::App().prime().whisper().downloader().changes()
		)) | rpl::map([=] {
			return ModelStatus(index);
		}) | rpl::distinct_until_changed();
	};
	for (auto i = 0; i != W::ModelCount(); ++i) {
		AddButtonWithLabel(
			content,
			rpl::single(QString::fromUtf8(W::Model(i).title)),
			status(i),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			ShowModelBox(controller, i);
		});
	}
	Ui::AddSkip(content);

	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, TextValue("Recognition", "Распознавание"));
	AddButtonWithLabel(
		content,
		TextValue("Language", "Язык"),
		rpl::single(rpl::empty) | rpl::then(
			Core::App().prime().whisper().changes()
		) | rpl::map([] {
			return LanguageName(Core::App().prime().whisper().language());
		}),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(TextValue("Language", "Язык"));
			for (const auto &language : kLanguages) {
				const auto code = QString::fromLatin1(language.code);
				AddButtonWithIcon(
					box->verticalLayout(),
					rpl::single(Text(language.en, language.ru)),
					st::settingsButtonNoIcon
				)->addClickHandler([=] {
					Core::App().prime().whisper().setLanguage(code);
					box->closeBox();
				});
			}
			box->addButton(TextValue("Close", "Закрыть"), [=] {
				box->closeBox();
			});
		}));
	});
	AddButtonWithLabel(
		content,
		TextValue("Processor threads", "Потоки процессора"),
		rpl::single(rpl::empty) | rpl::then(
			Core::App().prime().whisper().changes()
		) | rpl::map([] {
			return QString::number(Core::App().prime().whisper().threads());
		}),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(TextValue(
				"Processor threads",
				"Потоки процессора"));
			const auto max = std::max(2, QThread::idealThreadCount());
			for (auto count = 1; count <= max; ++count) {
				AddButtonWithIcon(
					box->verticalLayout(),
					rpl::single(QString::number(count)),
					st::settingsButtonNoIcon
				)->addClickHandler([=] {
					Core::App().prime().whisper().setThreads(count);
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

} // namespace Settings
