// dllmain.cpp : Définit le point d'entrée de l'application DLL.
#include <fstream>
#include <queue>
#include <functional>
#include <mutex>
#include <filesystem>

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
static std::string omessages[5];
static bool isInit=false;
static bool displayCapture = true;

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
		if (displayCapture && monsters[id].Capture != 0) {
			bool isAdd = false;
			int c = monsterMessages[monster].size();
			for (int i = 0; i < c; i++) {
				LOG(INFO) << monsterMessages[monster].front().first;
				if (!isAdd && monsters[id].Capture / 100 >= monsterMessages[monster].front().first) {
					monsterMessages[monster].push({ monsters[id].Capture / 100, omessages[0] });
					isAdd = true;
				}
				monsterMessages[monster].push(monsterMessages[monster].front());
				monsterMessages[monster].pop();
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
		lastMessage = monsters[monsterId].Name + monsterQueue.front().second;
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
	if (monsterSizeMultiplier >= Monster.Gold) {
		size = omessages[1];
	}
	else if (monsterSizeMultiplier >= Monster.Silver) {
		size = omessages[2];
	}
	else if (monsterSizeMultiplier <= Monster.Mini) {
		size = omessages[3];
	}
	else {
		size = omessages[4];
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
	std::string nativePCPath = "nativePC";

	// check if using ICE mod
	if (std::filesystem::exists("ICE")) {
		if (std::filesystem::is_directory("ICE")) {
			nativePCPath = "ICE/ntPC";
		}

		// check only first 3 digit for ICE
		/*if (std::string(GameVersion).rfind("314", 0) != 0) {
			LOG(ERR) << "Health Notes : Wrong ICE version";
			return false;
		}*/
	}
	/*else {
		if (std::string(GameVersion) != "421652") {
			LOG(ERR) << "Health Notes : Wrong version";
			return false;
		}
	}*/

	std::ifstream file(nativePCPath.append("/plugins/HealthNotes.json"));
	if (file.fail()) {
		LOG(ERR) << "Health notes : Monster data file not found";
		return false;
	}

	LOG(INFO) << "Health notes loading";
	
	nlohmann::json ConfigFile = nlohmann::json::object();
	file >> ConfigFile;

	language = ConfigFile["Language"];
	displayCapture = ConfigFile["DisplayCapture"];
	omessages[0] = ConfigFile["Capture"];
	omessages[1] = ConfigFile["Bigcrown"];
	omessages[2] = ConfigFile["Bigsilver"];
	omessages[3] = ConfigFile["Smallcrown"];
	omessages[4] = ConfigFile["Normalsize"];
	for (auto obj : ConfigFile["RatioMessages"])
	{
		messages.push({ obj["ratio"], obj["msg"] });
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

