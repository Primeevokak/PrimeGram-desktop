/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#include "prime/privacy/privacy_passcode.h"

#include "core/application.h"
#include "prime/prime_hub.h"
#include "prime/prime_prefs.h"
#include "prime/prime_text.h"

#include <QtCore/QDateTime>
#include <QtCore/QMessageAuthenticationCode>
#include <QtCore/QRandomGenerator>
#include <QtNetwork/QPasswordDigestor>

namespace Prime::Privacy {
namespace {

constexpr auto kSaltKey = std::string_view("prime_emergency_salt");
constexpr auto kVerifierKey = std::string_view("prime_emergency_verifier");
constexpr auto kFailuresKey = std::string_view("prime_lock_failures");
constexpr auto kDeadlineKey = std::string_view("prime_lock_deadline");
constexpr auto kIterations = 200'000;
constexpr auto kSaltSize = 32;
constexpr auto kFreeAttempts = 3;
constexpr auto kStepMs = qint64(5) * 60 * 1000;
constexpr auto kMaxDelayMs = qint64(24) * 60 * 60 * 1000;

[[nodiscard]] QByteArray Derive(
		const QString &passcode,
		const QByteArray &salt) {
	const auto key = QPasswordDigestor::deriveKeyPbkdf2(
		QCryptographicHash::Sha256,
		passcode.toUtf8(),
		salt,
		kIterations,
		32);
	// The stored value is an HMAC over a fixed domain string, never the
	// derived key itself.
	return QMessageAuthenticationCode::hash(
		QByteArray("PrimeGram emergency passcode v1"),
		key,
		QCryptographicHash::Sha256);
}

[[nodiscard]] bool ConstantTimeEqual(
		const QByteArray &a,
		const QByteArray &b) {
	if (a.size() != b.size()) {
		return false;
	}
	auto diff = uchar(0);
	for (auto i = 0; i != a.size(); ++i) {
		diff |= uchar(a[i]) ^ uchar(b[i]);
	}
	return diff == 0;
}

[[nodiscard]] qint64 NowMs() {
	return QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] qint64 DelayFor(int failures) {
	if (failures <= kFreeAttempts) {
		return 0;
	}
	return std::min(
		kMaxDelayMs,
		qint64(failures - kFreeAttempts) * kStepMs);
}

} // namespace

bool EmergencyPasscodeSet() {
	return !Prefs::GetBytes(kVerifierKey).isEmpty();
}

void SetEmergencyPasscode(const QString &passcode) {
	auto salt = QByteArray(kSaltSize, Qt::Uninitialized);
	QRandomGenerator::system()->generate(
		reinterpret_cast<quint32*>(salt.data()),
		reinterpret_cast<quint32*>(salt.data()) + kSaltSize / 4);
	Prefs::SetBytes(kSaltKey, salt);
	Prefs::SetBytes(kVerifierKey, Derive(passcode, salt));
}

void ClearEmergencyPasscode() {
	Prefs::SetBytes(kSaltKey, QByteArray());
	Prefs::SetBytes(kVerifierKey, QByteArray());
}

bool MatchesEmergencyPasscode(const QString &passcode) {
	if (passcode.isEmpty() || !EmergencyPasscodeSet()) {
		return false;
	}
	return ConstantTimeEqual(
		Derive(passcode, Prefs::GetBytes(kSaltKey)),
		Prefs::GetBytes(kVerifierKey));
}

void RunEmergencyWipe() {
	Core::App().prime().e2e().wipeAll();
	ClearEmergencyPasscode();
	Prefs::SetInt(kFailuresKey, 0);
	Prefs::SetString(kDeadlineKey, QString());
	// The same path as "I forgot my passcode": every account is logged
	// out and local data is removed.
	Core::App().logout();
}

qint64 LockoutRemainingMs() {
	const auto deadline = Prefs::GetString(kDeadlineKey).toLongLong();
	const auto now = NowMs();
	if (deadline <= 0) {
		return 0;
	}
	const auto left = deadline - now;
	// A clock wound far back must not shorten the wait.
	if (left > kMaxDelayMs) {
		return kMaxDelayMs;
	}
	return std::max<qint64>(left, 0);
}

void RegisterPasscodeFailure() {
	const auto failures = Prefs::GetInt(kFailuresKey, 0) + 1;
	Prefs::SetInt(kFailuresKey, failures);
	const auto delay = DelayFor(failures);
	Prefs::SetString(
		kDeadlineKey,
		delay ? QString::number(NowMs() + delay) : QString());
}

void RegisterPasscodeSuccess() {
	Prefs::SetInt(kFailuresKey, 0);
	Prefs::SetString(kDeadlineKey, QString());
}

QString FormatLockout(qint64 ms) {
	const auto minutes = std::max<qint64>(1, (ms + 59'999) / 60'000);
	if (minutes >= 120) {
		return Text("%1 h", "%1 ч").arg((minutes + 59) / 60);
	}
	return Text("%1 min", "%1 мин").arg(minutes);
}

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <wincred.h>

namespace {
constexpr auto kCredTargetName = L"PrimeGram/WindowsHello";
} // namespace

bool HasStoredWindowsHelloPasscode() {
	PCREDENTIALW pCred = nullptr;
	if (CredReadW(kCredTargetName, CRED_TYPE_GENERIC, 0, &pCred)) {
		CredFree(pCred);
		return true;
	}
	return false;
}

void StoreWindowsHelloPasscode(const QString &passcode) {
	if (passcode.isEmpty()) {
		ClearStoredWindowsHelloPasscode();
		return;
	}
	const auto bytes = passcode.toUtf8();
	DATA_BLOB inBlob;
	inBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()));
	inBlob.cbData = static_cast<DWORD>(bytes.size());

	DATA_BLOB outBlob;
	if (!CryptProtectData(&inBlob, L"PrimeGram Passcode", nullptr, nullptr, nullptr, 0, &outBlob)) {
		return;
	}

	CREDENTIALW cred = {};
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = const_cast<LPWSTR>(kCredTargetName);
	cred.CredentialBlobSize = outBlob.cbData;
	cred.CredentialBlob = outBlob.pbData;
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

	CredWriteW(&cred, 0);
	LocalFree(outBlob.pbData);
}

std::optional<QString> GetStoredWindowsHelloPasscode() {
	PCREDENTIALW pCred = nullptr;
	if (!CredReadW(kCredTargetName, CRED_TYPE_GENERIC, 0, &pCred)) {
		return std::nullopt;
	}

	DATA_BLOB inBlob;
	inBlob.pbData = pCred->CredentialBlob;
	inBlob.cbData = pCred->CredentialBlobSize;

	DATA_BLOB outBlob;
	const auto success = CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob);
	CredFree(pCred);

	if (!success) {
		return std::nullopt;
	}
	const auto result = QString::fromUtf8(
		reinterpret_cast<const char*>(outBlob.pbData),
		static_cast<int>(outBlob.cbData));
	LocalFree(outBlob.pbData);
	return result;
}

void ClearStoredWindowsHelloPasscode() {
	CredDeleteW(kCredTargetName, CRED_TYPE_GENERIC, 0);
}
#else
bool HasStoredWindowsHelloPasscode() { return false; }
void StoreWindowsHelloPasscode(const QString &passcode) {}
std::optional<QString> GetStoredWindowsHelloPasscode() { return std::nullopt; }
void ClearStoredWindowsHelloPasscode() {}
#endif

} // namespace Prime::Privacy
