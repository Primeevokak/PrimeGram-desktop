/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "settings/sections/settings_prime_storage.h"

#include "main/main_session.h"
#include "prime/cache/prime_cache.h"
#include "prime/prime_text.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
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
namespace PC = Prime::Cache;

} // namespace

PrimeStorage::PrimeStorage(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> PrimeStorage::title() {
	return TextValue("Storage and Cache", "Память и кэш");
}

void PrimeStorage::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto session = &controller->session();

	const auto statsVar = content->lifetime().make_state<
		rpl::variable<PC::CacheStats>>();

	const auto refresh = [=] {
		PC::Measure(session, [=](PC::CacheStats stats) {
			*statsVar = stats;
		});
	};
	refresh();

	Ui::AddSkip(content);

	// Total cache display with clear all button
	AddButtonWithLabel(
		content,
		TextValue("Total cache used", "Занято кэшем всего"),
		statsVar->value() | rpl::map([](const PC::CacheStats &s) {
			return PC::FormatSize(s.total());
		}),
		st::settingsButtonNoIcon
	);

	AddButtonWithIcon(
		content,
		TextValue("Clear all cache", "Очистить весь кэш"),
		st::settingsAttentionButtonWithIcon,
		{ &st::menuIconDelete }
	)->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(TextValue("Clear cache?", "Очистить кэш?"));
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				TextValue(
					"All cached media will be deleted from your disk. "
					"Files will remain safely in the Telegram cloud "
					"and will reload whenever you need them.",
					"Все кэшированные медиафайлы будут удалены с диска. "
					"Они останутся в облаке Telegram и при необходимости "
					"загрузятся снова."),
				st::boxLabel));
			box->addButton(
				TextValue("Clear All", "Очистить всё"),
				[=] {
					PC::ClearAll(session, [=] {
						controller->window().showToast(
							Text("Cache cleared", "Весь кэш очищен"));
						refresh();
					});
					box->closeBox();
				},
				st::attentionBoxButton);
			box->addButton(TextValue("Cancel", "Отмена"), [=] {
				box->closeBox();
			});
		}));
	});

	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"Cache categories",
			"Категории кэша (нажмите для очистки конкретной)"));

	const auto addCategoryRow = [&](PC::Kind kind, rpl::producer<QString> sizeText) {
		AddButtonWithLabel(
			content,
			rpl::single(PC::KindTitle(kind)),
			std::move(sizeText),
			st::settingsButtonNoIcon
		)->addClickHandler([=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(rpl::single(PC::KindTitle(kind)));
				box->addRow(object_ptr<Ui::FlatLabel>(
					box,
					TextValue(
						"Clear this category? Media will reload from the cloud when needed.",
						"Очистить эту категорию? Медиафайлы загрузятся снова при необходимости."),
					st::boxLabel));
				box->addButton(
					TextValue("Clear", "Очистить"),
					[=] {
						PC::Clear(session, kind, [=] {
							controller->window().showToast(
								Text("Cleared", "Категория очищена"));
							refresh();
						});
						box->closeBox();
					},
					st::attentionBoxButton);
				box->addButton(TextValue("Cancel", "Отмена"), [=] {
					box->closeBox();
				});
			}));
		});
	};

	addCategoryRow(
		PC::Kind::Images,
		statsVar->value() | rpl::map([](const PC::CacheStats &s) {
			return PC::FormatSize(s.images);
		}));

	addCategoryRow(
		PC::Kind::VideosAndDocs,
		statsVar->value() | rpl::map([](const PC::CacheStats &s) {
			return PC::FormatSize(s.videosAndDocs);
		}));

	addCategoryRow(
		PC::Kind::StickersAndGifs,
		statsVar->value() | rpl::map([](const PC::CacheStats &s) {
			return PC::FormatSize(s.stickersAndGifs);
		}));

	addCategoryRow(
		PC::Kind::AudioAndVoice,
		statsVar->value() | rpl::map([](const PC::CacheStats &s) {
			return PC::FormatSize(s.audioAndVoice);
		}));

	addCategoryRow(
		PC::Kind::TempFiles,
		statsVar->value() | rpl::map([](const PC::CacheStats &s) {
			return PC::FormatSize(s.tempFiles);
		}));

	Ui::AddSkip(content);
	Ui::AddDividerText(
		content,
		TextValue(
			"Cleaning cache frees up disk space without losing chat history.",
			"Очистка кэша освобождает диск без потери истории сообщений."));

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
