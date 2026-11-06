/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/tgws/tgws_balancer.h"

#include <QtCore/QRandomGenerator>

#include <algorithm>

namespace Prime::TgWs {
namespace {

constexpr auto kDcIds = { 1, 2, 3, 4, 5, 203 };

const char *const kEncodedDomains[] = {
	"virkgj.com",
	"vmmzovy.com",
	"mkuosckvso.com",
	"zaewayzmplad.com",
	"twdmbzcm.com",
	"awzwsldi.com",
	"clngqrflngqin.com",
	"tjacxbqtj.com",
	"bxaxtxmrw.com",
	"dmohrsgmohcrwb.com",
	"vwbmtmoi.com",
	"khgrre.com",
	"ulihssf.com",
	"tmhqsdqmfpmk.com",
	"xwuwoqbm.com",
	"orgcnunpj.com",
	"zhkuldz.com",
	"zypoljnslxa.com",
	"efabnxaowuzs.com",
	"zaftuzsftqdq.com",
};

} // namespace

QString DecodeCfDomain(const QString &encoded) {
	if (!encoded.endsWith(QString::fromLatin1(".com"))) {
		return encoded;
	}
	const auto body = encoded.left(encoded.size() - 4);
	auto letters = 0;
	for (const auto ch : body) {
		if (ch.isLetter()) {
			++letters;
		}
	}
	auto result = QString();
	result.reserve(body.size() + 6);
	for (const auto ch : body) {
		if (!ch.isLetter()) {
			result.append(ch);
			continue;
		}
		const auto base = (ch.unicode() > '`') ? int('a') : int('A');
		const auto shifted = ((int(ch.unicode()) - base - letters) % 26 + 26) % 26;
		result.append(QChar(base + shifted));
	}
	return result + QString::fromLatin1(".co.uk");
}

QStringList ParseCfDomainList(const QByteArray &text) {
	auto result = QStringList();
	const auto lines = text.split('\n');
	for (const auto &raw : lines) {
		const auto line = QString::fromUtf8(raw).trimmed();
		if (line.isEmpty() || line.startsWith(QChar('#'))) {
			continue;
		}
		result.push_back(DecodeCfDomain(line));
	}
	return result;
}

QStringList DefaultCfDomains() {
	auto result = QStringList();
	for (const auto encoded : kEncodedDomains) {
		result.push_back(DecodeCfDomain(QString::fromLatin1(encoded)));
	}
	return result;
}

QStringList CoerceDomainList(const QString &text) {
	auto normalized = text;
	normalized.replace(QChar(','), QChar(' '));
	normalized.replace(QChar(';'), QChar(' '));
	auto result = QStringList();
	auto seen = QStringList();
	for (const auto &item : normalized.split(
			QChar(' '),
			Qt::SkipEmptyParts)) {
		const auto trimmed = item.trimmed();
		const auto lower = trimmed.toLower();
		if (trimmed.isEmpty() || seen.contains(lower)) {
			continue;
		}
		seen.push_back(lower);
		result.push_back(trimmed);
	}
	return result;
}

void Balancer::setDomains(const QStringList &domains) {
	if (_domains == domains) {
		return;
	}
	_domains = domains;
	_active.clear();
	if (_domains.isEmpty()) {
		return;
	}
	for (const auto dc : kDcIds) {
		const auto index = QRandomGenerator::global()->bounded(
			int(_domains.size()));
		_active[dc] = _domains[index];
	}
}

const QStringList &Balancer::domains() const {
	return _domains;
}

QStringList Balancer::domainsForDc(int dcId) const {
	auto result = QStringList();
	const auto i = _active.find(dcId);
	const auto current = (i != _active.end()) ? i->second : QString();
	if (!current.isEmpty()) {
		result.push_back(current);
	}
	auto rest = _domains;
	std::shuffle(
		rest.begin(),
		rest.end(),
		*QRandomGenerator::global());
	for (const auto &domain : rest) {
		if (domain != current) {
			result.push_back(domain);
		}
	}
	return result;
}

bool Balancer::setActiveForDc(int dcId, const QString &domain) {
	const auto i = _active.find(dcId);
	if (i != _active.end() && i->second == domain) {
		return false;
	}
	_active[dcId] = domain;
	return true;
}

} // namespace Prime::TgWs
