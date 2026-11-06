/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "prime/tgws/tgws_context.h"

#include <QtCore/QString>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>

class QThread;

namespace Prime::TgWs {

class Worker;

enum class EngineState {
	Stopped,
	Starting,
	Running,
	Failed,
};

class Engine final {
public:
	Engine();
	~Engine();

	void start(const Config &config);
	void stop();
	bool waitUntilSettled(int timeoutMs);

	[[nodiscard]] EngineState state() const;
	[[nodiscard]] int boundPort() const;
	[[nodiscard]] QString lastError() const;
	[[nodiscard]] Stats stats() const;
	[[nodiscard]] QStringList logLines() const;
	void clearLog();

	void probeDomain(
		const QString &baseDomain,
		int dcId,
		std::function<void(int)> done);
	[[nodiscard]] QStringList cfDomains() const;

	[[nodiscard]] QString proxyLink() const;
	[[nodiscard]] QString secretHex() const;

	std::function<void(const QString &)> onLogLine;
	std::function<void()> onStateChanged;

private:
	void ensureThread();
	void setState(EngineState state, const QString &error = QString());

	std::unique_ptr<QThread> _thread;
	std::unique_ptr<Worker> _worker;
	std::shared_ptr<Context> _context;
	std::atomic<EngineState> _state = EngineState::Stopped;
	std::atomic<int> _port = 0;
	mutable std::mutex _mutex;
	std::condition_variable _settled;
	QString _error;
	QStringList _cfDomains;
	Config _config;
	Stats _stats;

};

} // namespace Prime::TgWs
