// cl /std:c++17 /EHsc /utf-8 /I<SourceFiles> bigfile_format_test.cpp ..\bigfile_format.cpp
#include "prime/bigfile/bigfile_format.h"

#include <cstdio>

using namespace Prime::BigFile;

namespace {

int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			++failures; \
			std::printf("FAIL %d: %s\n", __LINE__, #cond); \
		} \
	} while (false)

} // namespace

int main() {
	// The example from the Android sources.
	const auto h = ParseHeader(
		"[PG1 a7f3k2c9 03-17 7947285432M] Название файла.mkv");
	CHECK(h.has_value());
	if (h) {
		CHECK(h->alias == "a7f3k2c9" && h->index == 3 && h->total == 17);
		CHECK(h->totalSize == 7947285432LL * kMiB);
		CHECK(h->fileName == "Название файла.mkv");
	}
	CHECK(MakeHeader("a7f3k2c9", 3, 17, 7947285432LL * kMiB, "x.mkv")
		== "[PG1 a7f3k2c9 03-17 7947285432M] x.mkv");
	CHECK(MakeHeader("abcd1234", 1, 4, 1, "f") == "[PG1 abcd1234 1-4 1M] f");
	CHECK(!ParseHeader("movie.mkv"));
	CHECK(!ParseHeader("[PG1 ab 1-2 5M] x"));
	CHECK(!ParseHeader("[PG1 abcd1234 3-2 5M] x"));
	CHECK(!ParseHeader("[PG1 abcd1234 0-2 5M] x"));
	CHECK(!ParseHeader("[PG1 abcd1234 1-2 5M] "));

	auto longName = std::string();
	for (auto i = 0; i != 80; ++i) {
		longName += "Фильм";
	}
	longName += ".mkv";
	const auto name = MakeHeader("abcd1234", 2, 9, 5 * kGiB, longName);
	CHECK(name.size() <= 250);
	const auto parsed = ParseHeader(name);
	CHECK(parsed.has_value());
	if (parsed) {
		CHECK(parsed->fileName.size() > 4);
		CHECK(parsed->fileName.substr(parsed->fileName.size() - 4) == ".mkv");
	}

	const auto size = 7 * kGiB + 12345;
	const auto plan = MakePlan("big.bin", size, kChunkFree);
	auto sum = int64_t(0);
	for (auto i = 0; i != plan.count(); ++i) {
		CHECK(plan.parts[i].offset == sum);
		sum += plan.parts[i].length;
		if (i + 1 != plan.count()) {
			CHECK(plan.parts[i].length <= kChunkFree - kJitterMin);
		}
	}
	CHECK(sum == size);
	CHECK(plan.count() == 4);
	CHECK(MakePlan("e", 0, kChunkFree).count() == 1);
	CHECK(NewAlias().size() == 8);

	std::printf(failures ? "FAILED: %d\n" : "ALL OK\n", failures);
	return failures ? 1 : 0;
}
