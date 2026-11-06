/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/tgws/tgws_context.h"
#include "prime/tgws/tgws_link.h"
#include "prime/tgws/tgws_splitter.h"

#include <QtCore/QElapsedTimer>

namespace Prime::TgWs {

class Session final {
public:
	Session(Context &context, std::unique_ptr<Link> client, QString label);
	~Session();

	void start();
	void abort();

	std::function<void()> onFinished;

private:
	enum class Phase {
		ReadingInit,
		Connecting,
		Bridging,
		Rejected,
		Finished,
	};

	void clientData(const QByteArray &data);
	void clientClosed();
	void acceptInit(const Handshake &handshake);
	void route();

	void tryDirect(const QString &target, const std::string &dcKey);
	void directFailed(
		const QString &target,
		const std::string &dcKey,
		bool timedOut,
		bool failedRedirect,
		bool allRedirects);
	void tryFronting(
		const QString &target,
		std::function<void(WsPtr)> done);

	void startFallback();
	void nextFallback();
	void tryWorkers(std::function<void(bool)> done);
	void tryWorker(
		QStringList domains,
		int index,
		std::function<void(bool)> done);
	void tryCfProxy(std::function<void(bool)> done);
	void tryRawTcp(std::function<void(bool)> done);

	void bridge(std::unique_ptr<Link> upstream, bool useSplitter);
	void uplink(const QByteArray &data);
	void downlink(const QByteArray &data);
	void finish(const QString &reason);

	[[nodiscard]] QString dcTag() const;
	void log(const QString &line);

	Context &_context;
	std::unique_ptr<Link> _client;
	std::unique_ptr<Link> _upstream;
	QString _label;
	Phase _phase = Phase::ReadingInit;
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);

	Bytes _initBuffer;
	Bytes _pending;
	Handshake _relayInit = {};
	std::optional<ClientInit> _init;
	std::unique_ptr<CryptoCtx> _crypto;
	std::unique_ptr<MsgSplitter> _splitter;
	std::vector<int> _fallbackSteps;
	size_t _fallbackIndex = 0;

	uint64_t _bytesUp = 0;
	uint64_t _bytesDown = 0;
	QElapsedTimer _timer;
	QString _route;

};

} // namespace Prime::TgWs
