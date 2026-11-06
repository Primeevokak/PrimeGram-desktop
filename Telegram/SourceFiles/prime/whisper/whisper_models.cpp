/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/whisper/whisper_models.h"

#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Prime::Whisper {
namespace {

constexpr auto kBaseUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";

constexpr ModelInfo kModels[] = {
	{
		"tiny", "ggml-tiny-q5_1.bin", "Tiny",
		32'600'000,
		"fast, understands clear speech",
		"быстро, разборчивую речь узнаёт",
	},
	{
		"base", "ggml-base-q5_1.bin", "Base",
		60'000'000,
		"noticeably more accurate, a reasonable choice",
		"заметно точнее, разумный выбор",
	},
	{
		"small", "ggml-small-q5_1.bin", "Small",
		190'000'000,
		"accurate, a good default for a computer",
		"точный, хороший выбор для компьютера",
	},
	{
		"medium", "ggml-medium-q5_0.bin", "Medium",
		539'212'467,
		"more accurate, slower",
		"точнее, но медленнее",
		true,
	},
	{
		"turbo", "ggml-large-v3-turbo-q5_0.bin", "Large v3 Turbo",
		574'041'195,
		"best quality for its speed",
		"лучшее качество при такой скорости",
		true,
	},
	{
		"turbo-full", "ggml-large-v3-turbo.bin", "Large v3 Turbo (full precision)",
		1'624'555'275,
		"the turbo model without compression",
		"turbo без сжатия весов",
		true,
	},
	{
		"large", "ggml-large-v3-q5_0.bin", "Large v3",
		1'081'140'203,
		"the most accurate, needs a strong computer",
		"самая точная, нужен мощный компьютер",
		true,
	},
	{
		"large-full", "ggml-large-v3.bin", "Large v3 (full precision)",
		3'095'033'483,
		"maximum accuracy, 3 GB",
		"максимальная точность, 3 ГБ",
		true,
	},
};

} // namespace

int ModelCount() {
	return int(std::size(kModels));
}

const ModelInfo &Model(int index) {
	return kModels[std::clamp(index, 0, ModelCount() - 1)];
}

int FindModel(const QString &id) {
	for (auto i = 0; i != ModelCount(); ++i) {
		if (id == QLatin1String(kModels[i].id)) {
			return i;
		}
	}
	return -1;
}

QString ModelsDirectory() {
	const auto path = cWorkingDir() + u"tdata/prime_models/"_q;
	QDir().mkpath(path);
	return path;
}

QString ModelPath(int index) {
	return ModelsDirectory() + QLatin1String(Model(index).file);
}

bool IsInstalled(int index) {
	const auto info = QFileInfo(ModelPath(index));
	// Allow for the table being approximate: a real model is never tiny.
	return info.exists() && info.size() > Model(index).bytes / 2;
}

int64 InstalledSize(int index) {
	return IsInstalled(index) ? QFileInfo(ModelPath(index)).size() : 0;
}

void Remove(int index) {
	QFile::remove(ModelPath(index));
	QFile::remove(ModelPath(index) + u".part"_q);
}

Downloader::Downloader() = default;

Downloader::~Downloader() {
	cancel();
}

void Downloader::start(int index) {
	if (_index >= 0 || index < 0 || index >= ModelCount()) {
		return;
	}
	_index = index;
	_error = QString();
	_received = 0;
	_total = 0;
	if (!_network) {
		_network = std::make_unique<QNetworkAccessManager>();
	}
	const auto part = ModelPath(index) + u".part"_q;
	_resumed = QFileInfo(part).exists() ? QFileInfo(part).size() : 0;
	_file = std::make_unique<QFile>(part);
	if (!_file->open(QIODevice::WriteOnly | QIODevice::Append)) {
		_error = u"Cannot write the file"_q;
		finish(false);
		return;
	}
	auto request = QNetworkRequest(QUrl(
		QString::fromLatin1(kBaseUrl) + QLatin1String(Model(index).file)));
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	if (_resumed > 0) {
		request.setRawHeader(
			"Range",
			"bytes=" + QByteArray::number(_resumed) + "-");
	}
	_reply = _network->get(request);
	QObject::connect(_reply, &QNetworkReply::readyRead, [=] {
		flush();
	});
	QObject::connect(_reply, &QNetworkReply::downloadProgress, [=](
			qint64 received,
			qint64 total) {
		_received = received;
		_total = total;
		_changes.fire({});
	});
	QObject::connect(_reply, &QNetworkReply::finished, [=] {
		flush();
		const auto reply = _reply;
		if (!reply) {
			return;
		}
		const auto code = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		auto ok = (reply->error() == QNetworkReply::NoError);
		if (!ok) {
			_error = reply->errorString();
		} else if (_resumed > 0 && code == 200) {
			// The server ignored the range: start over.
			_file->resize(0);
			ok = false;
			_error = u"Please try again"_q;
		}
		finish(ok);
	});
	_changes.fire({});
}

void Downloader::flush() {
	if (_reply && _file) {
		const auto data = _reply->readAll();
		if (!data.isEmpty()) {
			_file->write(data);
		}
	}
}

float64 Downloader::progress() const {
	const auto total = _resumed + _total;
	return (total > 0)
		? std::clamp(float64(_resumed + _received) / total, 0., 1.)
		: 0.;
}

void Downloader::finish(bool ok) {
	const auto index = _index;
	if (_file) {
		_file->close();
		if (ok) {
			const auto part = _file->fileName();
			const auto target = ModelPath(index);
			QFile::remove(target);
			if (!QFile::rename(part, target)) {
				_error = u"Cannot save the model"_q;
				ok = false;
			}
		}
		_file = nullptr;
	}
	if (_reply) {
		const auto reply = std::exchange(_reply, nullptr);
		reply->disconnect();
		reply->deleteLater();
	}
	_index = -1;
	_changes.fire({});
}

void Downloader::cancel() {
	if (_index < 0) {
		return;
	}
	if (_reply) {
		_reply->abort();
	}
	if (_file) {
		_file->close();
		_file = nullptr;
	}
	if (_reply) {
		const auto reply = std::exchange(_reply, nullptr);
		reply->disconnect();
		reply->deleteLater();
	}
	_index = -1;
	_changes.fire({});
}

} // namespace Prime::Whisper
