// dllmain.cpp : Définit le point d'entrée de l'application DLL.
#include "MinHook.h"
#include "json.hpp"
#include "loader.h"
#include "plugin-utils.h"
#include "signatures.h"
#include <fstream>
#include <mutex>
#include <queue>
#include <windows.h>
#include <filesystem>

struct Monster{
	int Id;
	std::string Name;
	float Capture;
	float Mini;
	float Silver;
	float Gold;
};


static std::queue<std::pair<float, std::string>> messages;
static std::map<void *, std::queue<std::pair<float, std::string>>> monsterMessages;
static std::map<void*, bool> monsterChecked;
static std::mutex lock;
static struct Monster monsters[102];
static std::string language;
static std::string omessages[5];
static bool isInit=false;
static bool displayCapture = true;

using namespace loader;
using namespace plugin;

std::string_view plugin::module_name = "HealthNotes";

const int UENEMY_LEA_VFT_OFFSET = 0x43;
const int CHAT_INSTANCE_OFFSET = 0x4d;

static void *chat_instance = nullptr;
static void (*show_message)(void *, const char *, float, unsigned int, char) = nullptr;

void showMessage(std::string message) { show_message(*(void **)chat_instance, message.c_str(), -1, -1, 0); }

void handleMonsterCreated(int id, void *monster) {
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

void checkHealth(void *monster) {
	int monsterId = *offsetPtr<int>(monster, 0x12280);
  void *healthMgr = *offsetPtr<void *>(monster, 0x7670);
  float health = *offsetPtr<float>(healthMgr, 0x64);
  float maxHealth = *offsetPtr<float>(healthMgr, 0x60);

	if (!(health == 100 && maxHealth == 100)) {
		isInit = true;
	}

  char *monsterPath = offsetPtr<char>(monster, 0x7741);
  auto &monsterQueue = monsterMessages[monster];
  std::string lastMessage;
  while (!monsterQueue.empty() && health / maxHealth < monsterQueue.front().first) {
    lastMessage = monsterQueue.front().second;
    std::string re = "monstername";
		while (lastMessage.find(re) != std::string::npos) {
			size_t pos = lastMessage.find(re);
			lastMessage.replace(pos, re.length(), monsters[monsterId].Name);
		}
    monsterQueue.pop();
  }
  if (!lastMessage.empty()) {
    log(INFO, "Message: {}", lastMessage);
    showMessage(lastMessage);
  }
}

void checkMonsterSize(void* monster) {
	int monsterId = *offsetPtr<int>(monster, 0x12280);
	float sizeModifier = *offsetPtr<float>(monster, 0x7730);
	float sizeMultiplier = *offsetPtr<float>(monster, 0x184);
	if (sizeModifier <= 0 || sizeModifier >= 2) {
		sizeModifier = 1;
	}
	float monsterSizeMultiplier = static_cast<float>(std::round(sizeMultiplier / sizeModifier * 100.0f)) / 100.0f;

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

	std::string re = "monstername";
	while (size.find(re) != std::string::npos) {
		size_t pos = size.find(re);
		size.replace(pos, re.length(), Monster.Name);
	}

	showMessage(size);
}

byte *get_lea_addr(byte *addr) {
  auto base_addr = addr + 7; // size of call instruction
  auto offset = *reinterpret_cast<int *>(addr + 3);
  return base_addr + offset;
}

__declspec(dllexport) extern bool Load() {
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
    log(ERR, "Error: config file not found");
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

  auto uenemy_ctor_addr = find_func(sig::monster_ctor);
  auto uenemy_reset_addr = find_func(sig::monster_reset);
  auto show_message_addr = find_func(sig::show_message);
  auto message_instance_addr = find_func(sig::chat_instance_source);
  if (!uenemy_ctor_addr || !show_message_addr || !message_instance_addr || !uenemy_reset_addr)
    return false;

  show_message = reinterpret_cast<decltype(show_message)>(*show_message_addr);
  chat_instance = get_lea_addr(*message_instance_addr + CHAT_INSTANCE_OFFSET);
  log(INFO, "chat_instance:{:p}", chat_instance);

  MH_Initialize();

  Hook<void *(void *, int, int)>::hook(*uenemy_ctor_addr, [](auto orig, auto this_, auto id, auto subid) {
    auto ret = orig(this_, id, subid);
    handleMonsterCreated(id, this_);
    return ret;
  });
  Hook<void *(void *)>::hook(*uenemy_reset_addr, [](auto orig, auto this_) {
    {
      std::unique_lock l(lock);
      if (monsterMessages.erase(this_)) {
        log(INFO, "Monster {:p} removed", this_);
      }
      monsterChecked.erase(this_);
    }
    return orig(this_);
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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
  if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    return Load();
  return TRUE;
}
