// The installer (fomod/) agrees with the shipped files: every location in
// locations.txt is offered by ModuleConfig.xml and has a built-in layout, and
// the first is the shipped default.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	std::filesystem::path Source() { return std::filesystem::path(CHARSELECT_SOURCE_DIR); }

	std::string ReadAll(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		REQUIRE(in);
		std::ostringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	std::string Lower(std::string a_text)
	{
		std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_text;
	}

	std::size_t Count(const std::string& a_text, const std::string& a_what)
	{
		std::size_t n = 0;
		for (auto at = a_text.find(a_what); at != std::string::npos; at = a_text.find(a_what, at + 1)) {
			++n;
		}
		return n;
	}

	// folder, cell editor ID
	std::vector<std::pair<std::string, std::string>> Locations()
	{
		std::vector<std::pair<std::string, std::string>> out;
		std::istringstream in(ReadAll(Source() / "fomod" / "locations.txt"));
		for (std::string line; std::getline(in, line);) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty() || line.front() == '#') {
				continue;
			}
			const auto eq = line.find('=');
			REQUIRE(eq != std::string::npos);
			out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
		}
		return out;
	}
}

TEST_CASE("The installer offers every location, each with a layout", "[fomod][shipped]")
{
	const auto locations = Locations();
	REQUIRE(locations.size() >= 2);
	const auto config = ReadAll(Source() / "fomod" / "ModuleConfig.xml");
	const auto layouts = Lower(ReadAll(Source() / "dist-files" / "SKSE" / "Plugins" / "CharSelectLayouts.ini"));

	CHECK(Count(config, "source=\"Locations\\") == locations.size());
	for (const auto& [folder, cell] : locations) {
		INFO(folder << " = " << cell);
		CHECK(Count(config, "source=\"Locations\\" + folder + "\"") == 1);
		CHECK(Count(config, "<image path=\"fomod\\images\\" + folder + ".jpg\" />") == 1);
		CHECK(Count(layouts, "\n[" + Lower(cell) + "]") == 1);
	}
	// One choice preselected: the first, which is also the shipped default.
	CHECK(Count(config, "<type name=\"Recommended\" />") == 1);
	const auto recommended = config.find("<type name=\"Recommended\" />");
	const auto first = config.find("source=\"Locations\\" + locations.front().first + "\"");
	const auto second = config.find("source=\"Locations\\" + locations[1].first + "\"");
	CHECK(first < recommended);
	CHECK(recommended < second);
}

TEST_CASE("The shipped CharSelect.ini uses the installer's default location", "[fomod][shipped]")
{
	const auto ini = ReadAll(Source() / "dist-files" / "SKSE" / "Plugins" / "CharSelect.ini");
	// The packaging step replaces this one line per location.
	CHECK(Count(ini, "\nsLobbyCell = ") == 1);
	CHECK(Count(ini, "\nsLobbyCell = " + Locations().front().second + "\r\n") + Count(ini, "\nsLobbyCell = " + Locations().front().second + "\n") == 1);
}
