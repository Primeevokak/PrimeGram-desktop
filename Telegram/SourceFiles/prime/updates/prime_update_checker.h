#pragma once

#include <QtCore/QString>
#include <QtCore/QUrl>
#include <rpl/variable.h>
#include <rpl/producer.h>

namespace Prime::Updates {

enum class State {
	Idle,
	Checking,
	Latest,
	Available,
	Error,
};

struct ReleaseInfo {
	QString tag;
	QString title;
	QString changelog;
	QString htmlUrl;
	QString downloadUrl;
	uint64 assetSize = 0;
};

class Checker final : public QObject {
public:
	static Checker &Instance();

	void check(bool manual = false);
	void openLatestRelease();
	void downloadUpdate();

	[[nodiscard]] rpl::producer<State> state() const {
		return _state.value();
	}
	[[nodiscard]] State currentState() const {
		return _state.current();
	}
	[[nodiscard]] const ReleaseInfo &latestInfo() const {
		return _info;
	}

private:
	Checker();

	void parseResponse(const QByteArray &bytes, bool manual);
	void showUpdateBox(const ReleaseInfo &info);

	rpl::variable<State> _state = State::Idle;
	ReleaseInfo _info;
	bool _checking = false;
};

void Check(bool manual = false);
void OpenReleasePage();

} // namespace Prime::Updates
