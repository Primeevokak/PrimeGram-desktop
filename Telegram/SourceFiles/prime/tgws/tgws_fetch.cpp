/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_fetch.h"

#include <QtCore/QTimer>
#include <QtNetwork/QSslSocket>

#include <memory>

namespace Prime::TgWs {
namespace {

constexpr auto kMaxResponse = 1024 * 1024;

[[nodiscard]] QByteArray Dechunk(const QByteArray &body) {
	auto result = QByteArray();
	auto position = 0;
	while (position < body.size()) {
		const auto lineEnd = body.indexOf("\r\n", position);
		if (lineEnd < 0) {
			break;
		}
		auto ok = false;
		const auto size = body.mid(position, lineEnd - position)
			.split(';').front().trimmed().toInt(&ok, 16);
		if (!ok || size < 0) {
			break;
		} else if (size == 0) {
			return result;
		}
		const auto start = lineEnd + 2;
		if (start + size > body.size()) {
			break;
		}
		result.append(body.mid(start, size));
		position = start + size + 2;
	}
	return result;
}

} // namespace

QByteArray DecodeHttpBody(const QByteArray &response) {
	const auto split = response.indexOf("\r\n\r\n");
	if (split < 0) {
		return {};
	}
	const auto head = response.left(split).toLower();
	const auto body = response.mid(split + 4);
	if (!head.startsWith("http/1.1 200") && !head.startsWith("http/1.0 200")) {
		return {};
	}
	if (head.contains("transfer-encoding: chunked")) {
		return Dechunk(body);
	}
	return body;
}

void HttpsGetPinned(
		const QString &ip,
		const QString &host,
		const QString &path,
		int timeoutMs,
		std::function<void(QByteArray)> done) {
	auto socket = new QSslSocket();
	socket->setPeerVerifyName(host);
	const auto buffer = std::make_shared<QByteArray>();
	const auto finished = std::make_shared<bool>(false);
	const auto finish = [=](bool ok) {
		if (*finished) {
			return;
		}
		*finished = true;
		const auto result = ok ? DecodeHttpBody(*buffer) : QByteArray();
		socket->disconnect();
		socket->abort();
		socket->deleteLater();
		done(result);
	};
	QObject::connect(socket, &QSslSocket::encrypted, socket, [=] {
		const auto request = QString::fromLatin1(
			"GET %1 HTTP/1.1\r\nHost: %2\r\nUser-Agent: tg-ws-proxy\r\n"
			"Accept: */*\r\nConnection: close\r\n\r\n")
			.arg(path)
			.arg(host).toLatin1();
		socket->write(request);
	});
	QObject::connect(socket, &QSslSocket::readyRead, socket, [=] {
		buffer->append(socket->readAll());
		if (buffer->size() > kMaxResponse) {
			finish(false);
		}
	});
	QObject::connect(socket, &QSslSocket::disconnected, socket, [=] {
		finish(true);
	});
	QObject::connect(socket, &QSslSocket::errorOccurred, socket, [=](
			QAbstractSocket::SocketError error) {
		finish(error == QAbstractSocket::RemoteHostClosedError);
	});
	QTimer::singleShot(timeoutMs, socket, [=] {
		finish(false);
	});
	socket->connectToHostEncrypted(ip, 443, host);
}

} // namespace Prime::TgWs
