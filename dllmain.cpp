// dllmain.cpp : Définit le point d'entrée de l'application DLL.
#include <fstream>
#include <queue>
#include <functional>
#include <mutex>

#include <windows.h>

#include "minhook/MinHook.h"
#include "json/json.hpp"
#include "loader.h"
#include "ghidra_export.h"
#include "util.h"

struct Monster{
	int Id;
	std::string Name;
	float Capture;
	float Mini;
	float Silver;
	float Gold;
};

static std::queue<std::pair<float, std::string>> messages;
static std::map<void*, std::queue<std::pair<float, std::string>>> monsterMessages;
static std::map<void*, bool> monsterChecked;
static std::mutex lock;
static struct Monster monsters[102];
static std::string language;
static bool isInit=false;

using namespace loader;

void showMessage(std::string message) {
	MH::Chat::ShowGameMessage(*(undefined**)MH::Chat::MainPtr, (undefined*) &message[0], -1, -1, 0);
}

void handleMonsterCreated(int id, undefined* monster)
{
	char* monsterPath = offsetPtr<char>(monster, 0x7741);
	if (monsterPath[2] == '0' || monsterPath[2] == '1') {
		LOG(INFO) << "Setting up health messages for " << monsterPath;
		std::unique_lock l(lock);
		monsterMessages[monster] = messages;
		if (monsters[id].Capture != 0) {
			if (language == "us") {
				monsterMessages[monster].push({ monsters[id].Capture / 100, "<STYL MOJI_RED_DEFAULT>" + monsters[id].Name + " is ready to be captured(" + std::to_string(int(monsters[id].Capture)) + "%)" + "</STYL>" });
			}
			else if (language == "jp") {
				monsterMessages[monster].push({ monsters[id].Capture / 100, "<STYL MOJI_RED_DEFAULT>" + monsters[id].Name + "を捕獲できます(" + std::to_string(int(monsters[id].Capture)) + "%)" + "</STYL>" });
			}
			else {
				monsterMessages[monster].push({ monsters[id].Capture / 100, "<STYL MOJI_RED_DEFAULT>" + monsters[id].Name + "可以捕获了(" + std::to_string(int(monsters[id].Capture)) + "%)" + "</STYL>" });
			}
		}
		isInit = false;
		monsterChecked[monster] = false;
	}
}

void checkHealth(void* monster) {
	int monsterId = *offsetPtr<int>(monster, 0x12280);
	void* healthMgr = *offsetPtr<void*>(monster, 0x7670);
	float health = *offsetPtr<float>(healthMgr, 0x64);
	float maxHealth = *offsetPtr<float>(healthMgr, 0x60);

	if (!(health == 100 && maxHealth == 100)) {
		isInit = true;
	}

	char* monsterPath = offsetPtr<char>(monster, 0x7741);
	auto& monsterQueue = monsterMessages[monster];
	std::string lastMessage;
	while (!monsterQueue.empty() && health / maxHealth < monsterQueue.front().first) {
		lastMessage = "<STYL MOJI_YELLOW_DEFAULT>" + monsters[monsterId].Name + monsterQueue.front().second + "</STYL>";
		LOG(INFO) << "Message: " << lastMessage;
		showMessage(lastMessage);
		monsterQueue.pop();
	}
}

void checkMonsterSize(void* monster) {
	int monsterId = *offsetPtr<int>(monster, 0x12280);
	float sizeModifier = *offsetPtr<float>(monster, 0x7730);
	float sizeMultiplier = *offsetPtr<float>(monster, 0x188);
	if (sizeModifier <= 0 || sizeModifier >= 2) {
		sizeModifier = 1;
	}
	float monsterSizeMultiplier = sizeMultiplier / sizeModifier;

	LOG(INFO) << "monster id: " << monsterId << ", size: " << monsterSizeMultiplier;

	struct Monster Monster = monsters[monsterId];

	std::string size;
	if (language == "us") {
		if (monsterSizeMultiplier >= Monster.Gold) {
			size = " size is <STYL MOJI_RED_DEFAULT>big crown</STYL>";
		}
		else if (monsterSizeMultiplier >= Monster.Silver) {
			size = " size is <STYL MOJI_YELLOW_DEFAULT>big silver</STYL>";
		}
		else if (monsterSizeMultiplier <= Monster.Mini) {
			size = " size is <STYL MOJI_RED_DEFAULT>small crown</STYL>";
		}
		else {
			size = " size is normal size";
		}
	}
	else if (language == "jp") {
		if (monsterSizeMultiplier >= Monster.Gold) {
			size = "サイズは<STYL MOJI_RED_DEFAULT>大金</STYL>";
		}
		else if (monsterSizeMultiplier >= Monster.Silver) {
			size = "サイズは<STYL MOJI_YELLOW_DEFAULT>大银</STYL>";
		}
		else if (monsterSizeMultiplier <= Monster.Mini) {
			size = "サイズは<STYL MOJI_RED_DEFAULT>小金</STYL>";
		}
		else {
			size = "サイズは通常サイズ";
		}
	}
	else {
		if (monsterSizeMultiplier >= Monster.Gold) {
			size = "尺寸是<STYL MOJI_RED_DEFAULT>大金</STYL>";
		}
		else if (monsterSizeMultiplier >= Monster.Silver) {
			size = "尺寸是<STYL MOJI_YELLOW_DEFAULT>大银</STYL>";
		}
		else if (monsterSizeMultiplier <= Monster.Mini) {
			size = "尺寸是<STYL MOJI_RED_DEFAULT>小金</STYL>";
		}
		else {
			size = "尺寸是普通大小";
		}
	}

	std::stringstream ss;
	ss << Monster.Name << size;
	std::string msg = ss.str();
	showMessage(msg);
}

CreateHook(MH::Monster::ctor, ConstructMonster, void*, void* this_ptr, unsigned int monster_id, unsigned int variant)
{
	LOG(INFO) << "Creating Monster : " << monster_id << "-" << variant << " @0x" << this_ptr;
	return original(this_ptr, monster_id, variant);
}

__declspec(dllexport) extern bool Load()
{
	if (std::string(GameVersion) != "421652") {
		LOG(ERR) << "Health Notes : Wrong version";
		return false;
	}

	std::ifstream file("nativePC/plugins/HealthNotes.json");
	if (file.fail()) {
		LOG(ERR) << "Health notes : Monster data file not found";
		return false;
	}

	LOG(INFO) << "Health notes loading";
	
	nlohmann::json ConfigFile = nlohmann::json::object();
	file >> ConfigFile;

	language = ConfigFile["Language"];

	for (auto ratio : ConfigFile["HealthRatio"])
	{
		if (language == "us") {
			messages.push({ ratio, " have " + std::to_string(int(double(ratio) * 100)) + "% remaining hp" });
		}
		else if(language == "jp"){
			messages.push({ ratio, "のhpは残り" + std::to_string(int(double(ratio) * 100)) + "%" });
		}
		else {
			messages.push({ ratio, "血量还剩" + std::to_string(int(double(ratio) * 100)) + "%" });
		}
	}
	int index = 0;
	for (auto obj : ConfigFile["Monsters"])
	{
		index = obj["Id"];
		monsters[index].Id = obj["Id"];
		if (language == "us") {
			monsters[index].Name = obj["USName"];
		}
		else if (language == "jp") {
			monsters[index].Name = obj["JPName"];
		}
		else {
			monsters[index].Name = obj["Name"];
		}
		monsters[index].Capture = obj["Capture"];
		monsters[index].Mini = obj["Mini"];
		monsters[index].Silver = obj["Silver"];
		monsters[index].Gold = obj["Gold"];
		index++;
	}

	MH_Initialize();

	HookLambda(MH::Monster::ctor,
		[](auto monster, auto id, auto subId) {
			auto ret = original(monster, id, subId);
			handleMonsterCreated(id, monster);
			return ret;
		});
	HookLambda(MH::Monster::dtor,
		[](auto monster) {
			LOG(INFO) << "Monster destroyed " << (void*)monster;
			{
				std::unique_lock l(lock);
				monsterMessages.erase(monster);
				monsterChecked.erase(monster);
			}
			return original(monster);
		});

	std::thread([]() {
		while (true) {
			std::this_thread::sleep_for(std::chrono::seconds(2));
			{
				std::unique_lock l(lock);
				for (auto [monster, queue] : monsterMessages) {
					checkHealth(monster);
				}
				if (isInit) {
					for (auto [monster, isChecked] : monsterChecked) {
						if (!isChecked) {
							checkMonsterSize(monster);
							monsterChecked[monster] = true;
						}
						
					}
					
				}
			}
		}
	}).detach();

	MH_ApplyQueued();

	return true;
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
		return Load();
    return TRUE;
}

