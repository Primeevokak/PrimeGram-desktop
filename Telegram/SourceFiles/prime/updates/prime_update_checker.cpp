#include "prime/updates/prime_update_checker.h"

#include "core/version.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "core/click_handler_types.h"
#include "base/debug_log.h"
#include "prime/prime_text.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>

namespace Prime::Updates {
namespace {

constexpr auto kGitHubReleasesApi = "https://api.github.com/repos/Primeevokak/PrimeGram-desktop/releases/latest";
constexpr auto kGitHubReleasesWeb = "https://github.com/Primeevokak/PrimeGram-desktop/releases";

[[nodiscard]] int64 ParseVersionNumber(const QString &tag) {
	// e.g. "v7.4.1" or "7.4.0" -> 7004001
	auto cleaned = tag;
	if (cleaned.startsWith('v', Qt::CaseInsensitive)) {
		cleaned = cleaned.mid(1);
	}
	const auto parts = cleaned.split('.');
	if (parts.isEmpty()) {
		return 0;
	}
	int64 major = parts.value(0).toInt();
	int64 minor = parts.size() > 1 ? parts.value(1).toInt() : 0;
	int64 patch = parts.size() > 2 ? parts.value(2).toInt() : 0;
	return major * 1000000 + minor * 1000 + patch;
}

} // namespace

Checker &Checker::Instance() {
	static Checker instance;
	return instance;
}

Checker::Checker() {
}

void Checker::check(bool manual) {
	if (_checking) {
		return;
	}
	_checking = true;
	_state = State::Checking;

	const auto manager = new QNetworkAccessManager(this);
	auto request = QNetworkRequest(QUrl(kGitHubReleasesApi));
	request.setRawHeader("Accept", "application/vnd.github.v3+json");
	request.setRawHeader("User-Agent", "PrimeGram-Desktop");

	const auto reply = manager->get(request);
	connect(reply, &QNetworkReply::finished, [=] {
		_checking = false;
		reply->deleteLater();
		manager->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Prime Update: Network error: %1").arg(reply->errorString()));
			_state = State::Error;
			if (manual) {
				Ui::Toast::Show(Prime::Text(
					"Could not check for updates. Open releases page?",
					"Не удалось проверить обновления. Открыть страницу релизов?"));
			}
			return;
		}

		parseResponse(reply->readAll(), manual);
	});
}

void Checker::parseResponse(const QByteArray &bytes, bool manual) {
	const auto doc = QJsonDocument::fromJson(bytes);
	if (!doc.isObject()) {
		_state = State::Error;
		return;
	}
	const auto root = doc.object();
	const auto tag = root.value("tag_name").toString();
	const auto title = root.value("name").toString();
	const auto body = root.value("body").toString();
	const auto htmlUrl = root.value("html_url").toString(kGitHubReleasesWeb);

	QString downloadUrl = htmlUrl;
	uint64 assetSize = 0;

	const auto assets = root.value("assets").toArray();
	for (const auto &val : assets) {
		const auto asset = val.toObject();
		const auto name = asset.value("name").toString();
		if (name.endsWith(".exe", Qt::CaseInsensitive)
			|| name.endsWith(".zip", Qt::CaseInsensitive)) {
			downloadUrl = asset.value("browser_download_url").toString();
			assetSize = asset.value("size").toVariant().toULongLong();
			break;
		}
	}

	_info = ReleaseInfo{
		.tag = tag,
		.title = title.isEmpty() ? tag : title,
		.changelog = body,
		.htmlUrl = htmlUrl,
		.downloadUrl = downloadUrl,
		.assetSize = assetSize,
	};

	const auto remoteVer = ParseVersionNumber(tag);
	const auto currentVer = int64(AppVersion);

	if (remoteVer > currentVer) {
		_state = State::Available;
		if (manual) {
			showUpdateBox(_info);
		} else {
			Ui::Toast::Show(Prime::Text(
				QString("PrimeGram update available: %1").arg(tag),
				QString("Доступно обновление PrimeGram: %1").arg(tag)));
		}
	} else {
		_state = State::Latest;
		if (manual) {
			Ui::Toast::Show(Prime::Text(
				"You have the latest version of PrimeGram Desktop.",
				"У вас установлена актуальная версия PrimeGram Desktop."));
		}
	}
}

void Checker::showUpdateBox(const ReleaseInfo &info) {
	auto message = Prime::Text(
		QString("A new version of PrimeGram Desktop is available: %1\n\n%2").arg(info.title, info.changelog.left(500)),
		QString("Доступна новая версия PrimeGram Desktop: %1\n\n%2").arg(info.title, info.changelog.left(500)));

	Ui::show(Ui::MakeConfirmBox({
		.text = message,
		.confirmed = [=](Fn<void()> close) {
			close();
			downloadUpdate();
		},
		.confirmText = Prime::Text("Download", "Скачать"),
		.cancelText = Prime::Text("Later", "Позже"),
	}));
}

void Checker::openLatestRelease() {
	const auto url = _info.htmlUrl.isEmpty() ? QString(kGitHubReleasesWeb) : _info.htmlUrl;
	UrlClickHandler::Open(url);
}

void Checker::downloadUpdate() {
	const auto url = _info.downloadUrl.isEmpty() ? QString(kGitHubReleasesWeb) : _info.downloadUrl;
	UrlClickHandler::Open(url);
}

void Check(bool manual) {
	Checker::Instance().check(manual);
}

void OpenReleasePage() {
	Checker::Instance().openLatestRelease();
}

} // namespace Prime::Updates
