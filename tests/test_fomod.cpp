// The installer (fomod/) agrees with the shipped files: every location in
// locations.txt and every main menu choice in mainmenu.txt is offered by
// ModuleConfig.xml, which installs Locations\<location>\<choice> for every pair
// of them, every location has a built-in layout, and the first of each is the
// shipped default. (What StageFomod writes into those folders isn't run here.)

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

	// The <name>=<value> lines of one of fomod's lists.
	std::vector<std::pair<std::string, std::string>> ReadList(const char* a_file)
	{
		std::vector<std::pair<std::string, std::string>> out;
		std::istringstream in(ReadAll(Source() / "fomod" / a_file));
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

	// folder, cell editor ID
	std::vector<std::pair<std::string, std::string>> Locations() { return ReadList("locations.txt"); }
	// folder, "<bReplaceMainMenu>,<bReplaceAtStartup>,<bLobbyOnLoad>"
	std::vector<std::pair<std::string, std::string>> MainMenuModes() { return ReadList("mainmenu.txt"); }

	// The INI line "<key> = <value>" exactly once, whatever the line endings.
	std::size_t CountLine(const std::string& a_ini, const std::string& a_line)
	{
		return Count(a_ini, "\n" + a_line + "\r\n") + Count(a_ini, "\n" + a_line + "\n");
	}
}

TEST_CASE("The installer offers every location, each with a layout", "[fomod][shipped]")
{
	const auto locations = Locations();
	REQUIRE(locations.size() >= 2);
	const auto config = ReadAll(Source() / "fomod" / "ModuleConfig.xml");
	const auto layouts = Lower(ReadAll(Source() / "dist-files" / "SKSE" / "Plugins" / "CharSelectLayouts.ini"));

	CHECK(Count(config, "<flag name=\"location\">") == locations.size());
	for (const auto& [folder, cell] : locations) {
		INFO(folder << " = " << cell);
		CHECK(Count(config, "<flag name=\"location\">" + folder + "</flag>") == 1);
		CHECK(Count(config, "<image path=\"fomod\\images\\" + folder + ".jpg\" />") == 1);
		// One cell, or a random pick from several (separated by commas).
		std::istringstream cells(cell);
		for (std::string one; std::getline(cells, one, ',');) {
			INFO(one);
			CHECK(Count(layouts, "\n[" + Lower(one) + "]") == 1);
		}
	}
}

TEST_CASE("The installer offers every main menu choice", "[fomod][shipped]")
{
	const auto modes = MainMenuModes();
	REQUIRE(modes.size() >= 2);
	const auto config = ReadAll(Source() / "fomod" / "ModuleConfig.xml");
	CHECK(Count(config, "<flag name=\"mainmenu\">") == modes.size());
	for (const auto& [folder, values] : modes) {
		INFO(folder << " = " << values);
		CHECK(Count(config, "<flag name=\"mainmenu\">" + folder + "</flag>") == 1);
		// Three 0/1 values, like "1,0,1".
		REQUIRE(values.size() == 5);
		CHECK(values[1] == ',');
		CHECK(values[3] == ',');
		for (const char c : { values[0], values[2], values[4] }) {
			CHECK((c == '0' || c == '1'));
		}
	}
}

TEST_CASE("Every location and main menu choice installs its own CharSelect.ini", "[fomod][shipped]")
{
	auto config = ReadAll(Source() / "fomod" / "ModuleConfig.xml");
	std::erase(config, '\r');  // a checkout may have CRLF line endings
	const auto locations = Locations();
	const auto modes = MainMenuModes();
	CHECK(Count(config, "<pattern>") == locations.size() * modes.size());
	CHECK(Count(config, "source=\"Locations\\") == locations.size() * modes.size());
	for (const auto& location : locations) {
		for (const auto& mode : modes) {
			INFO(location.first << " / " << mode.first);
			const auto pattern = std::string("<flagDependency flag=\"location\" value=\"") + location.first + "\" />\n\t\t\t\t\t<flagDependency flag=\"mainmenu\" value=\"" + mode.first + "\" />\n\t\t\t\t</dependencies>\n\t\t\t\t<files>\n\t\t\t\t\t<folder source=\"Locations\\" + location.first + "\\" + mode.first + "\" destination=\"\" />";
			CHECK(Count(config, pattern) == 1);
		}
	}
}

TEST_CASE("The first location and main menu choice are preselected", "[fomod][shipped]")
{
	const auto config = ReadAll(Source() / "fomod" / "ModuleConfig.xml");
	CHECK(Count(config, "<type name=\"Recommended\" />") == 2);
	const auto firstRecommended = config.find("<type name=\"Recommended\" />");
	const auto secondRecommended = config.find("<type name=\"Recommended\" />", firstRecommended + 1);
	const auto locations = Locations();
	const auto modes = MainMenuModes();
	const auto location0 = config.find("<flag name=\"location\">" + locations[0].first + "<");
	const auto location1 = config.find("<flag name=\"location\">" + locations[1].first + "<");
	const auto mode0 = config.find("<flag name=\"mainmenu\">" + modes[0].first + "<");
	const auto mode1 = config.find("<flag name=\"mainmenu\">" + modes[1].first + "<");
	CHECK(location0 < firstRecommended);
	CHECK(firstRecommended < location1);
	CHECK(mode0 < secondRecommended);
	CHECK(secondRecommended < mode1);
}

TEST_CASE("The shipped CharSelect.ini uses the installer's defaults", "[fomod][shipped]")
{
	const auto ini = ReadAll(Source() / "dist-files" / "SKSE" / "Plugins" / "CharSelect.ini");
	// The packaging step replaces these lines for each location and choice.
	CHECK(Count(ini, "\nsLobbyCell = ") == 1);
	CHECK(Count(ini, "\nbReplaceMainMenu = ") == 1);
	CHECK(Count(ini, "\nbReplaceAtStartup = ") == 1);
	CHECK(Count(ini, "\nbLobbyOnLoad = ") == 1);
	CHECK(CountLine(ini, "sLobbyCell = " + Locations().front().second) == 1);
	const auto values = MainMenuModes().front().second;  // "<bReplaceMainMenu>,<bReplaceAtStartup>,<bLobbyOnLoad>"
	CHECK(CountLine(ini, std::string("bReplaceMainMenu = ") + values[0]) == 1);
	CHECK(CountLine(ini, std::string("bReplaceAtStartup = ") + values[2]) == 1);
	CHECK(CountLine(ini, std::string("bLobbyOnLoad = ") + values[4]) == 1);
}
