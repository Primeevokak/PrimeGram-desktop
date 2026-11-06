/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_engine.h"

#include "prime/tgws/tgws_fetch.h"
#include "prime/tgws/tgws_link.h"
#include "prime/tgws/tgws_session.h"

#include <QtCore/QRandomGenerator>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <chrono>
#include <map>

namespace Prime::TgWs {
namespace {

constexpr auto kPortScanRange = 20;
constexpr auto kDomainListIp = "185.199.109.133";
constexpr auto kDomainListHost = "raw.githubusercontent.com";
constexpr auto kDomainListPath
	= "/Flowseal/tg-ws-proxy/main/.github/cfproxy-domains.txt";
constexpr auto kMinValidDomains = 3;
constexpr auto kDomainRefreshMs = 60 * 60 * 1000;
constexpr auto kStatsPublishMs = 1000;

[[nodiscard]] bool IsValidDomain(const QString &domain) {
	if (domain.size() < 4 || domain.size() > 253 || !domain.contains(QChar('.'))) {
		return false;
	}
	for (const auto ch : domain) {
		if (!(ch.isLetterOrNumber() || ch == QChar('.') || ch == QChar('-'))) {
			return false;
		}
	}
	return !domain.startsWith(QChar('.')) && !domain.endsWith(QChar('.'));
}

} // namespace

class Worker final : public QObject {
public:
	Worker(Context *context, Engine *engine);
	~Worker();

	void start(const Config &config);
	void stop();

	std::function<void(int port)> onListening;
	std::function<void(const QString &)> onFailed;
	std::function<void(const Stats &)> onStats;
	std::function<void(const QStringList &)> onDomains;

	void probe(const QString &baseDomain, int dcId, std::function<void(int)> done);

private:
	void publishDomains();
	void accept();
	void warmUp();
	void refreshDomains();

	Context &_context;
	std::unique_ptr<QTcpServer> _server;
	std::map<Session*, std::unique_ptr<Session>> _sessions;
	QTimer *_statsTimer = nullptr;
	QTimer *_refreshTimer = nullptr;
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);

};

Worker::Worker(Context *context, Engine *)
: _context(*context) {
}

Worker::~Worker() {
	*_alive = false;
	stop();
}

void Worker::start(const Config &config) {
	stop();
	_context.reset();
	_context.config = config;
	_context.balancer.setDomains(config.cfUserDomains.isEmpty()
		? DefaultCfDomains()
		: config.cfUserDomains);
	publishDomains();

	_server = std::make_unique<QTcpServer>();
	const auto address = QHostAddress(config.host);
	auto listening = false;
	for (auto i = 0; i != kPortScanRange; ++i) {
		const auto port = config.port + i;
		if (port > 65535) {
			break;
		}
		if (_server->listen(address, quint16(port))) {
			listening = true;
			break;
		}
	}
	if (!listening) {
		const auto error = _server->errorString();
		_server.reset();
		_context.log.add(QString::fromLatin1("cannot listen on %1:%2: %3")
			.arg(config.host)
			.arg(config.port)
			.arg(error));
		if (onFailed) {
			onFailed(error);
		}
		return;
	}
	QObject::connect(_server.get(), &QTcpServer::newConnection, _server.get(), [=] {
		accept();
	});
	_context.log.add(QString::fromLatin1("MTProto proxy listening on %1:%2")
		.arg(config.host)
		.arg(_server->serverPort()));

	_statsTimer = new QTimer(this);
	QObject::connect(_statsTimer, &QTimer::timeout, _statsTimer, [=] {
		if (onStats) {
			onStats(_context.stats);
		}
	});
	_statsTimer->start(kStatsPublishMs);

	warmUp();
	if (config.fallbackCfProxy && config.cfUserDomains.isEmpty()) {
		refreshDomains();
		_refreshTimer = new QTimer(this);
		QObject::connect(_refreshTimer, &QTimer::timeout, _refreshTimer, [=] {
			refreshDomains();
		});
		_refreshTimer->start(kDomainRefreshMs);
	}
	if (onListening) {
		onListening(int(_server->serverPort()));
	}
}

void Worker::stop() {
	if (_statsTimer) {
		delete _statsTimer;
		_statsTimer = nullptr;
	}
	if (_refreshTimer) {
		delete _refreshTimer;
		_refreshTimer = nullptr;
	}
	if (_server) {
		_server->close();
		_server.reset();
	}
	auto sessions = std::move(_sessions);
	_sessions.clear();
	for (auto &entry : sessions) {
		entry.second->onFinished = nullptr;
		entry.second->abort();
	}
	_context.reset();
}

void Worker::accept() {
	while (_server && _server->hasPendingConnections()) {
		const auto socket = _server->nextPendingConnection();
		const auto label = QString::fromLatin1("%1:%2")
			.arg(socket->peerAddress().toString())
			.arg(socket->peerPort());
		auto session = std::make_unique<Session>(
			_context,
			std::make_unique<TcpLink>(socket),
			label);
		const auto raw = session.get();
		const auto alive = _alive;
		session->onFinished = [=] {
			QTimer::singleShot(0, [=] {
				if (*alive) {
					_sessions.erase(raw);
				}
			});
		};
		_sessions.emplace(raw, std::move(session));
		raw->start();
	}
}

void Worker::warmUp() {
	const auto context = &_context;
	for (const auto dc : { 2, 4 }) {
		const auto target = QString::fromStdString(RedirectIpForDc(dc));
		for (const auto isMedia : { false, true }) {
			const auto key = QString::number(dc)
				+ (isMedia ? QString::fromLatin1("m") : QString());
			auto hosts = QStringList();
			for (const auto &host : WsDomainsForDc(dc, isMedia)) {
				hosts.push_back(QString::fromStdString(host));
			}
			_context.wsPool.refill(
				key,
				_context.config.poolSize,
				MakeDirectMaker(context, target, hosts));
		}
	}
	_context.log.add(QString::fromLatin1("WS pool warm-up started"));
}

void Worker::publishDomains() {
	if (onDomains) {
		onDomains(_context.balancer.domains());
	}
}

void Worker::probe(
		const QString &baseDomain,
		int dcId,
		std::function<void(int)> done) {
	const auto started = NowMs();
	auto plan = ConnectPlan();
	plan.base.timeoutMs = 3'000;
	plan.hosts = QStringList{
		QString::fromLatin1("kws%1.").arg(dcId) + baseDomain
	};
	plan.ipIsHost = true;
	ConnectFirst(std::move(plan), [=](WsPtr ws) {
		done(ws ? int(NowMs() - started) : -1);
	});
}

void Worker::refreshDomains() {
	const auto alive = _alive;
	const auto path = QString::fromLatin1(kDomainListPath)
		+ QString::fromLatin1("?")
		+ QString::number(QRandomGenerator::global()->generate(), 36);
	HttpsGetPinned(
		QString::fromLatin1(kDomainListIp),
		QString::fromLatin1(kDomainListHost),
		path,
		10'000,
		[=](QByteArray body) {
			if (!*alive) {
				return;
			}
			auto valid = QStringList();
			for (const auto &domain : ParseCfDomainList(body)) {
				if (IsValidDomain(domain)) {
					valid.push_back(domain);
				}
			}
			if (valid.size() < kMinValidDomains) {
				_context.log.add(QString::fromLatin1(
					"CF domain list refresh failed, keeping %1 domains")
					.arg(_context.balancer.domains().size()));
				return;
			}
			_context.balancer.setDomains(valid);
			publishDomains();
			_context.log.add(QString::fromLatin1("CF domain list refreshed: %1 domains")
				.arg(valid.size()));
		});
}

Engine::Engine() = default;

Engine::~Engine() {
	stop();
	if (_thread) {
		_thread->quit();
		_thread->wait();
	}
	_worker.reset();
}

void Engine::ensureThread() {
	if (_thread) {
		return;
	}
	_context = std::make_shared<Context>();
	_context->log.onLine = [=](const QString &line) {
		if (onLogLine) {
			onLogLine(line);
		}
	};
	_thread = std::make_unique<QThread>();
	_thread->setObjectName(QString::fromLatin1("PrimeGram TgWs"));
	_worker = std::make_unique<Worker>(_context.get(), this);
	_worker->onListening = [=](int port) {
		_port = port;
		setState(EngineState::Running);
	};
	_worker->onFailed = [=](const QString &error) {
		_port = 0;
		setState(EngineState::Failed, error);
	};
	_worker->onStats = [=](const Stats &stats) {
		auto lock = std::lock_guard(_mutex);
		_stats = stats;
	};
	_worker->onDomains = [=](const QStringList &domains) {
		auto lock = std::lock_guard(_mutex);
		_cfDomains = domains;
	};
	_worker->moveToThread(_thread.get());
	_thread->start();
}

void Engine::start(const Config &config) {
	ensureThread();
	setState(EngineState::Starting);
	{
		auto lock = std::lock_guard(_mutex);
		_config = config;
		if (_config.secret == Secret()) {
			_config.secret = RandomSecret();
		}
	}
	const auto copy = [&] {
		auto lock = std::lock_guard(_mutex);
		return _config;
	}();
	const auto worker = _worker.get();
	QMetaObject::invokeMethod(worker, [=] {
		worker->start(copy);
	}, Qt::QueuedConnection);
}

void Engine::stop() {
	if (!_thread || _state == EngineState::Stopped) {
		return;
	}
	const auto worker = _worker.get();
	QMetaObject::invokeMethod(worker, [=] {
		worker->stop();
	}, Qt::BlockingQueuedConnection);
	_port = 0;
	{
		auto lock = std::lock_guard(_mutex);
		_stats = Stats();
	}
	setState(EngineState::Stopped);
}

bool Engine::waitUntilSettled(int timeoutMs) {
	auto lock = std::unique_lock(_mutex);
	return _settled.wait_for(
		lock,
		std::chrono::milliseconds(timeoutMs),
		[&] { return _state != EngineState::Starting; });
}

EngineState Engine::state() const {
	return _state;
}

int Engine::boundPort() const {
	return _port;
}

QString Engine::lastError() const {
	auto lock = std::lock_guard(_mutex);
	return _error;
}

Stats Engine::stats() const {
	auto lock = std::lock_guard(_mutex);
	return _stats;
}

QStringList Engine::logLines() const {
	return _context ? _context->log.lines() : QStringList();
}

void Engine::clearLog() {
	if (_context) {
		_context->log.clear();
	}
}

void Engine::probeDomain(
		const QString &baseDomain,
		int dcId,
		std::function<void(int)> done) {
	ensureThread();
	const auto worker = _worker.get();
	QMetaObject::invokeMethod(worker, [=] {
		worker->probe(baseDomain, dcId, done);
	}, Qt::QueuedConnection);
}

QStringList Engine::cfDomains() const {
	auto lock = std::lock_guard(_mutex);
	return _cfDomains;
}

QString Engine::secretHex() const {
	auto lock = std::lock_guard(_mutex);
	auto bytes = QByteArray(
		reinterpret_cast<const char*>(_config.secret.data()),
		int(_config.secret.size()));
	return QString::fromLatin1(bytes.toHex());
}

QString Engine::proxyLink() const {
	auto host = QString();
	{
		auto lock = std::lock_guard(_mutex);
		host = _config.host;
	}
	return QString::fromLatin1("tg://proxy?server=%1&port=%2&secret=dd%3")
		.arg(host)
		.arg(int(_port))
		.arg(secretHex());
}

void Engine::setState(EngineState state, const QString &error) {
	{
		auto lock = std::lock_guard(_mutex);
		_error = error;
		_state = state;
	}
	_settled.notify_all();
	if (onStateChanged) {
		onStateChanged();
	}
}

} // namespace Prime::TgWs
