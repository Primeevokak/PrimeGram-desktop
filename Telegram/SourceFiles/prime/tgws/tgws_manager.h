/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include "base/weak_ptr.h"
#include "mtproto/mtproto_proxy_data.h"
#include "prime/tgws/tgws_engine.h"

#include <rpl/event_stream.h>

namespace Prime::TgWs {

class Manager final : public base::has_weak_ptr {
public:
	Manager();
	~Manager();

	void start();
	void stop();

	[[nodiscard]] bool enabled() const;
	void setEnabled(bool value);

	[[nodiscard]] int configuredPort() const;
	void setConfiguredPort(int value);

	[[nodiscard]] QString pinnedDomain() const;
	void setPinnedDomain(const QString &value);

	[[nodiscard]] QStringList workers() const;
	void setWorkers(const QStringList &value);

	[[nodiscard]] Engine &engine();
	[[nodiscard]] rpl::producer<> changes() const;

	[[nodiscard]] bool isManaged(const MTP::ProxyData &proxy) const;
	[[nodiscard]] MTP::ProxyData proxyData() const;

private:
	[[nodiscard]] Config config() const;
	[[nodiscard]] Secret secret() const;
	[[nodiscard]] QString secretHex() const;
	void restartEngine();
	void syncProxyEntry(bool select);
	void removeProxyEntry();
	[[nodiscard]] bool removeStaleEntries();

	Engine _engine;
	rpl::event_stream<> _changes;

};

} // namespace Prime::TgWs
