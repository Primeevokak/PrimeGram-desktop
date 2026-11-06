/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <map>
#include <vector>

namespace Prime::TgWs {

[[nodiscard]] QString DecodeCfDomain(const QString &encoded);

[[nodiscard]] QStringList ParseCfDomainList(const QByteArray &text);

[[nodiscard]] QStringList DefaultCfDomains();

[[nodiscard]] QStringList CoerceDomainList(const QString &text);

class Balancer final {
public:
	void setDomains(const QStringList &domains);
	[[nodiscard]] const QStringList &domains() const;

	[[nodiscard]] QStringList domainsForDc(int dcId) const;
	bool setActiveForDc(int dcId, const QString &domain);

private:
	QStringList _domains;
	std::map<int, QString> _active;

};

} // namespace Prime::TgWs
