/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_manager.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "prime/prime_prefs.h"

#include <crl/crl.h>

#include <algorithm>

namespace Prime::TgWs {
namespace {

constexpr auto kEnabledKey = std::string_view("prime_tgws_enabled");
constexpr auto kPortKey = std::string_view("prime_tgws_port");
constexpr auto kSecretKey = std::string_view("prime_tgws_secret");
constexpr auto kDomainKey = std::string_view("prime_tgws_domain");
constexpr auto kWorkersKey = std::string_view("prime_tgws_workers");
constexpr auto kProxyAddedKey = std::string_view("prime_tgws_proxy_added");

constexpr auto kDefaultPort = 1443;
constexpr auto kStartWaitMs = 1500;

} // namespace

Manager::Manager() {
	_engine.onStateChanged = [=] {
		crl::on_main(this, [=] {
			_changes.fire({});
		});
	};
}

Manager::~Manager() {
	_engine.onStateChanged = nullptr;
	_engine.stop();
}

void Manager::start() {
	if (!enabled()) {
		removeProxyEntry();
		return;
	}
	restartEngine();
	const auto first = !Prime::Prefs::GetBool(kProxyAddedKey, false);
	syncProxyEntry(first);
	if (first) {
		Prime::Prefs::SetBool(kProxyAddedKey, true);
	}
}

void Manager::stop() {
	_engine.stop();
}

bool Manager::enabled() const {
	return Prime::Prefs::GetBool(kEnabledKey, true);
}

void Manager::setEnabled(bool value) {
	if (enabled() == value) {
		return;
	}
	Prime::Prefs::SetBool(kEnabledKey, value);
	if (value) {
		restartEngine();
		syncProxyEntry(true);
		Prime::Prefs::SetBool(kProxyAddedKey, true);
	} else {
		_engine.stop();
		removeProxyEntry();
	}
	_changes.fire({});
}

int Manager::configuredPort() const {
	const auto text = Prime::Prefs::GetBytes(kPortKey);
	auto ok = false;
	const auto value = text.toInt(&ok);
	return (ok && value >= 1024 && value <= 65535) ? value : kDefaultPort;
}

void Manager::setConfiguredPort(int value) {
	Prime::Prefs::SetBytes(kPortKey, QByteArray::number(value));
	if (enabled()) {
		restartEngine();
		syncProxyEntry(false);
	}
	_changes.fire({});
}

QString Manager::pinnedDomain() const {
	return QString::fromUtf8(Prime::Prefs::GetBytes(kDomainKey));
}

void Manager::setPinnedDomain(const QString &value) {
	Prime::Prefs::SetBytes(kDomainKey, value.trimmed().toUtf8());
	if (enabled()) {
		restartEngine();
		syncProxyEntry(false);
	}
	_changes.fire({});
}

QStringList Manager::workers() const {
	const auto text = QString::fromUtf8(Prime::Prefs::GetBytes(kWorkersKey));
	return text.split(QChar(' '), Qt::SkipEmptyParts);
}

void Manager::setWorkers(const QStringList &value) {
	Prime::Prefs::SetBytes(kWorkersKey, value.join(QChar(' ')).toUtf8());
	if (enabled()) {
		restartEngine();
		syncProxyEntry(false);
	}
	_changes.fire({});
}

Engine &Manager::engine() {
	return _engine;
}

rpl::producer<> Manager::changes() const {
	return _changes.events();
}

Secret Manager::secret() const {
	auto stored = Prime::Prefs::GetBytes(kSecretKey);
	if (stored.size() != int(kSecretLen)) {
		const auto fresh = RandomSecret();
		stored = QByteArray(
			reinterpret_cast<const char*>(fresh.data()),
			int(fresh.size()));
		Prime::Prefs::SetBytes(kSecretKey, stored);
	}
	auto result = Secret();
	std::copy(stored.begin(), stored.end(), result.begin());
	return result;
}

QString Manager::secretHex() const {
	const auto value = secret();
	return QString::fromLatin1(QByteArray(
		reinterpret_cast<const char*>(value.data()),
		int(value.size())).toHex());
}

Config Manager::config() const {
	auto result = Config();
	result.port = configuredPort();
	result.secret = secret();
	const auto domain = pinnedDomain();
	if (!domain.isEmpty()) {
		result.cfUserDomains = QStringList{ domain };
	}
	result.cfWorkerDomains = workers();
	return result;
}

void Manager::restartEngine() {
	_engine.stop();
	_engine.start(config());
	_engine.waitUntilSettled(kStartWaitMs);
}

MTP::ProxyData Manager::proxyData() const {
	auto result = MTP::ProxyData();
	result.type = MTP::ProxyData::Type::Mtproto;
	result.host = QString::fromLatin1("127.0.0.1");
	result.port = _engine.boundPort();
	result.password = QString::fromLatin1("dd") + secretHex();
	return result;
}

bool Manager::isManaged(const MTP::ProxyData &proxy) const {
	if (proxy.type != MTP::ProxyData::Type::Mtproto
		|| proxy.host != QString::fromLatin1("127.0.0.1")) {
		return false;
	}
	return proxy.password.endsWith(secretHex(), Qt::CaseInsensitive);
}

void Manager::syncProxyEntry(bool select) {
	if (_engine.state() != EngineState::Running) {
		return;
	}
	const auto wanted = proxyData();
	auto &proxies = Core::App().settings().proxy();
	select = removeStaleEntries() || select;
	auto &list = proxies.list();
	const auto i = std::find_if(list.begin(), list.end(), [&](const auto &p) {
		return isManaged(p);
	});
	if (i == list.end()) {
		if (select) {
			proxies.addToList(wanted);
			Core::App().setCurrentProxy(wanted, MTP::ProxyData::Settings::Enabled);
		}
		return;
	}
	const auto was = *i;
	if (was == wanted) {
		if (select) {
			Core::App().setCurrentProxy(wanted, MTP::ProxyData::Settings::Enabled);
		}
		return;
	}
	const auto wasSelected = (proxies.selected() == was);
	if (!proxies.replaceInList(was, wanted)) {
		return;
	}
	if (wasSelected || select) {
		Core::App().setCurrentProxy(
			wanted,
			select ? MTP::ProxyData::Settings::Enabled : proxies.settings());
	}
}

bool Manager::removeStaleEntries() {
	auto &proxies = Core::App().settings().proxy();
	auto stale = std::vector<MTP::ProxyData>();
	for (const auto &proxy : proxies.list()) {
		const auto ours = proxy.type == MTP::ProxyData::Type::Mtproto
			&& proxy.host == QString::fromLatin1("127.0.0.1")
			&& proxy.password.size() == 2 + 2 * int(kSecretLen)
			&& proxy.password.startsWith(QString::fromLatin1("dd"));
		if (ours && !isManaged(proxy)) {
			stale.push_back(proxy);
		}
	}
	auto selectedRemoved = false;
	for (const auto &proxy : stale) {
		const auto wasSelected = (proxies.selected() == proxy);
		if (proxies.removeFromList(proxy) && wasSelected) {
			selectedRemoved = true;
		}
	}
	return selectedRemoved;
}

void Manager::removeProxyEntry() {
	auto &proxies = Core::App().settings().proxy();
	const auto &list = proxies.list();
	const auto i = std::find_if(list.begin(), list.end(), [&](const auto &p) {
		return isManaged(p);
	});
	if (i == list.end()) {
		return;
	}
	const auto entry = *i;
	const auto wasSelected = (proxies.selected() == entry);
	if (!proxies.removeFromList(entry)) {
		return;
	}
	if (wasSelected) {
		Core::App().setCurrentProxy(
			MTP::ProxyData(),
			MTP::ProxyData::Settings::Disabled);
	}
}

} // namespace Prime::TgWs
