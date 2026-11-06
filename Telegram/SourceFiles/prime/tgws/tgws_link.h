/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/tgws/tgws_context.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>

class QTcpSocket;

namespace Prime::TgWs {

class Link {
public:
	virtual ~Link() = default;

	virtual void write(const Bytes &data) = 0;
	virtual void writeParts(const std::vector<Bytes> &parts) = 0;
	[[nodiscard]] virtual qint64 pending() const = 0;
	virtual void setPaused(bool paused) = 0;
	virtual void close() = 0;

	std::function<void(const QByteArray &)> onData;
	std::function<void()> onClosed;
	std::function<void()> onDrained;
};

class WsLink final : public Link {
public:
	explicit WsLink(WsPtr ws);

	void write(const Bytes &data) override;
	void writeParts(const std::vector<Bytes> &parts) override;
	[[nodiscard]] qint64 pending() const override;
	void setPaused(bool paused) override;
	void close() override;

private:
	WsPtr _ws;

};

class TcpLink final : public Link {
public:
	explicit TcpLink(QTcpSocket *socket);
	~TcpLink();

	static std::unique_ptr<TcpLink> Connect(
		const QString &host,
		int port,
		int timeoutMs,
		std::function<void(bool ok)> done);

	void write(const Bytes &data) override;
	void writeParts(const std::vector<Bytes> &parts) override;
	[[nodiscard]] qint64 pending() const override;
	void setPaused(bool paused) override;
	void close() override;

private:
	void attach();
	void readyRead();

	QPointer<QTcpSocket> _socket;
	bool _paused = false;
	bool _closedNotified = false;
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);

};

} // namespace Prime::TgWs
