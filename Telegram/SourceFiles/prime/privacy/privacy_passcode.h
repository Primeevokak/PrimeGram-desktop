/*
This file is part of PrimeGram Desktop.

PrimeGram Desktop is based on Telegram Desktop and is distributed under
the same license terms, see LEGAL in the repository root.
*/
#pragma once

namespace Prime::Privacy {

// An extra passcode that, when entered on the lock screen, erases the app:
// every account is logged out and local data is removed, so that the app
// looks freshly installed.
[[nodiscard]] bool EmergencyPasscodeSet();
void SetEmergencyPasscode(const QString &passcode);
void ClearEmergencyPasscode();
[[nodiscard]] bool MatchesEmergencyPasscode(const QString &passcode);
void RunEmergencyWipe();

// Progressive delay after wrong passcodes on the lock screen:
// three free attempts, then five more minutes for each further failure,
// up to a day. The state survives restarts.
[[nodiscard]] qint64 LockoutRemainingMs();
void RegisterPasscodeFailure();
void RegisterPasscodeSuccess();
[[nodiscard]] QString FormatLockout(qint64 ms);

// Secure storage for Windows Hello unlock across clean restarts (DPAPI).
[[nodiscard]] bool HasStoredWindowsHelloPasscode();
void StoreWindowsHelloPasscode(const QString &passcode);
[[nodiscard]] std::optional<QString> GetStoredWindowsHelloPasscode();
void ClearStoredWindowsHelloPasscode();

} // namespace Prime::Privacy
