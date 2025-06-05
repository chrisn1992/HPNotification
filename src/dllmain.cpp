// Pseudocode plan for optimization:
// 1. Remove unnecessary copies and use references where possible (especially for std::vector and std::string).
// 2. Avoid repeated lookups in std::map by using iterators or references.
// 3. Minimize string allocations and replacements in message building functions.
// 4. Use early returns to reduce nesting and improve readability.
// 5. Remove redundant code and comments, and combine similar logic where possible.
// 6. Use const correctness for function parameters where applicable.
// 7. Use structured bindings and modern C++20 features for clarity and performance.

#include "MinHook.h"
#include "json.hpp"
#include "loader.h"
#include "plugin-utils.h"
#include "signatures.h"
#include "supplemental.cpp"
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <windows.h>

std::string_view plugin::module_name = "HPNotification";

using namespace loader;
using namespace plugin;
using json = nlohmann::json;

static std::mutex slthreadLock;
static std::mutex HookListenerLok;

static HPNNS::Config *Configs = new HPNNS::Config();
static std::map<void *, std::vector<HPNNS::RatioMessage>> monsterMessages;
static std::map<void *, HPNNS::Monster> monsters;
static std::map<void *, std::pair<bool, bool>> monsterChecked;
static int intervalBase = 0, interval = 0, hp = 0, percent = 0;
const int UENEMY_LEA_VFT_OFFSET = 0x43;
const int CHAT_INSTANCE_OFFSET = 0x4d;
static void *chat_instance = nullptr;
static void (*show_message)(void *, const char *, float, unsigned int, char) = nullptr;
static bool (*CallBack)(void *monster) = nullptr;

inline void showMessage(const std::string &message, char param3 = 0) {
  show_message(*(void **)chat_instance, message.c_str(), -1, -1, param3);
}

void ValidateAndBuildMessageString(std::string &hpMessage, const std::vector<HPNNS::RatioMessage> &monsterQueue,
                                   const std::string &monsterNameReplacer, HPNNS::Monster *Monster,
                                   const std::string &hpRatioReplacer, float hpRatio, std::string &captureMessage) {
  if (Configs->DisplayHealth && !monsterQueue.empty()) {
    hpMessage = !monsterQueue.front().Msg.empty() ? monsterQueue.front().Msg : Configs->HpMessage;
    size_t pos;
    while ((pos = hpMessage.find(monsterNameReplacer)) != std::string::npos) {
      hpMessage.replace(pos, monsterNameReplacer.length(), Monster->Name);
    }
    while ((pos = hpMessage.find(hpRatioReplacer)) != std::string::npos) {
      hpMessage.replace(pos, hpRatioReplacer.length(), std::format("{:.2f}", hpRatio * 100.0f));
    }
  }
  if (Configs->DisplayCapture && Monster->Capture != 0 && !Monster->captureMessageShown) {
    if (hpRatio < Monster->Capture / 100.0f) {
      captureMessage = Configs->Capture;
      size_t pos;
      while ((pos = captureMessage.find(monsterNameReplacer)) != std::string::npos) {
        captureMessage.replace(pos, monsterNameReplacer.length(), Monster->Name);
      }
      Monster->captureMessageShown = true;
    }
  }
}

void DisplayFinalMessage(const std::string &captureMessage, std::string &hpMessage, HPNNS::Monster *Monster,
                         float health, float maxHealth, float newValue) {
  if (!captureMessage.empty()) {
    showMessage(captureMessage, 2);
  }
  if (!hpMessage.empty()) {
    if (Monster->captureMessageShown) {
      hpMessage = std::format("{} Monster can be captured now.", hpMessage);
    }
    hpMessage = std::format("{}\r\nHP: <STYL MOJI_GREEN_DEFAULT>{:.0f}</STYL>/<STYL MOJI_GREEN_DEFAULT>{:.0f}</STYL>",
                            hpMessage, health, maxHealth);
    showMessage(hpMessage, 2);
    Monster->prevValue = newValue;
  }
}

int BuildMonsterInformation(void *&monster, HPNNS::Monster *Monster, float &health, float &maxHealth,
                            const std::vector<HPNNS::RatioMessage> & /*monsterQueue*/, float &hpRatio) {
  if (!monsterMessages.contains(monster))
    return -1;
  void *healthMgr = *offsetPtr<void *>(monster, 0x7670);
  health = *offsetPtr<float>(healthMgr, 0x64);
  maxHealth = *offsetPtr<float>(healthMgr, 0x60);
  if (Monster->isPending)
    return 0;
  hpRatio = (health / maxHealth);
  if (Configs->EnableLogging) {
    LOG(INFO) << "Monster Information Loaded: " << Monster->Name << " Health: " << health << " MaxHealth: " << maxHealth
              << " hp ratio: " << hpRatio << "\r\n";
  }
  if (hpRatio <= 0.05f)
    return -1;
  return 1;
}

void ProcessDisplayMessage(std::string &hpMessage, const std::vector<HPNNS::RatioMessage> &monsterQueue,
                           const std::string &monsterNameReplacer, HPNNS::Monster *Monster,
                           const std::string &hpRatioReplacer, float hpRatio, std::string &captureMessage, float health,
                           float maxHealth, float newValue) {
  ValidateAndBuildMessageString(hpMessage, monsterQueue, monsterNameReplacer, Monster, hpRatioReplacer, hpRatio,
                                captureMessage);
  if (Configs->EnableLogging) {
    LOG(INFO) << "Monster Message Build: " << Monster->Name << " Health: " << health << " MaxHealth: " << maxHealth
              << " hp ratio: " << hpRatio << "\r\n";
  }
  DisplayFinalMessage(captureMessage, hpMessage, Monster, health, maxHealth, newValue);
  if (Configs->EnableLogging) {
    LOG(INFO) << "Monster Message Display: " << Monster->Name << " Health: " << health << " MaxHealth: " << maxHealth
              << " hp ratio: " << hpRatio << "\r\n";
  }
}

void RevalidateMaxHP(HPNNS::Monster *Monster, float maxHealth, void *&monster) {
  int intMaxHealth = static_cast<int>(std::round(maxHealth));
  if (Monster->MaxHealth != intMaxHealth) {
    monsterMessages[monster] = Configs->RatioMessages;
    Monster->MaxHealth = intMaxHealth;
    monsterChecked[monster].first = true;
    Monster->prevValue = -1;
  }
  if (Configs->EnableLogging) {
    LOG(INFO) << "Monster Max HP validate: " << Monster->Name << " MaxHealth: " << maxHealth << "\r\n";
  }
}

bool checkHealthQueue(void *monster) {
  auto &Monster = monsters[monster];
  auto &monsterQueue = monsterMessages[monster];
  float maxHealth = 0, health = 0, hpRatio = 0;
  std::string hpMessage, captureMessage;
  const std::string monsterNameReplacer = "monstername", hpRatioReplacer = "hpratio";
  int result = BuildMonsterInformation(monster, &Monster, health, maxHealth, monsterQueue, hpRatio);
  if (result == -1 || monsterQueue.empty())
    return false;
  if (!(health == 100 && maxHealth == 100))
    RevalidateMaxHP(&Monster, maxHealth, monster);
  while (!monsterQueue.empty() && result && hpRatio != 1.0f && hpRatio < monsterQueue.front().Ratio) {
    if (!monsterMessages.contains(monster))
      return false;
    Monster.isPending = true;
    ValidateAndBuildMessageString(hpMessage, monsterQueue, monsterNameReplacer, &Monster, hpRatioReplacer, hpRatio,
                                  captureMessage);
    monsterQueue.erase(monsterQueue.begin());
    Monster.isPending = false;
  }
  DisplayFinalMessage(captureMessage, hpMessage, &Monster, health, maxHealth, hpRatio);
  return true;
}

bool checkHealthInterval(void *monster) {
  auto &Monster = monsters[monster];
  auto &monsterQueue = monsterMessages[monster];
  float maxHealth = 0, health = 0, hpRatio = 0;
  std::string hpMessage, captureMessage;
  const std::string monsterNameReplacer = "monstername", hpRatioReplacer = "hpratio";
  int result = BuildMonsterInformation(monster, &Monster, health, maxHealth, monsterQueue, hpRatio);
  if (result == -1)
    return false;
  if (!(health == 100 && maxHealth == 100))
    RevalidateMaxHP(&Monster, maxHealth, monster);
  Monster.prevValue = (Monster.prevValue == -1) ? 1 : Monster.prevValue;
  if (result && hpRatio < Monster.prevValue) {
    ProcessDisplayMessage(hpMessage, monsterQueue, monsterNameReplacer, &Monster, hpRatioReplacer, hpRatio,
                          captureMessage, health, maxHealth, hpRatio);
  }
  Monster.isPending = false;
  return true;
}

bool checkHealthHP(void *monster) {
  auto &Monster = monsters[monster];
  auto &monsterQueue = monsterMessages[monster];
  float maxHealth = 0, health = 0, hpRatio = 0;
  std::string hpMessage, captureMessage;
  const std::string monsterNameReplacer = "monstername", hpRatioReplacer = "hpratio";
  int result = BuildMonsterInformation(monster, &Monster, health, maxHealth, monsterQueue, hpRatio);
  if (result == -1)
    return false;
  if (!(health == 100 && maxHealth == 100))
    RevalidateMaxHP(&Monster, maxHealth, monster);
  Monster.prevValue = (Monster.prevValue == -1) ? health : Monster.prevValue;
  if (health < Monster.prevValue && ((Monster.prevValue - health) > hp)) {
    ProcessDisplayMessage(hpMessage, monsterQueue, monsterNameReplacer, &Monster, hpRatioReplacer, hpRatio,
                          captureMessage, health, maxHealth, health);
  }
  return true;
}

bool checkHealthPerc(void *monster) {
  auto &Monster = monsters[monster];
  auto &monsterQueue = monsterMessages[monster];
  float maxHealth = 0, health = 0, hpRatio = 0;
  std::string hpMessage, captureMessage;
  const std::string monsterNameReplacer = "monstername", hpRatioReplacer = "hpratio";
  int result = BuildMonsterInformation(monster, &Monster, health, maxHealth, monsterQueue, hpRatio);
  if (result == -1)
    return false;
  if (!(health == 100 && maxHealth == 100))
    RevalidateMaxHP(&Monster, maxHealth, monster);
  Monster.prevValue = (Monster.prevValue == -1) ? 1 : Monster.prevValue;
  if (hpRatio <= Monster.prevValue && ((Monster.prevValue - hpRatio) > percent)) {
    ProcessDisplayMessage(hpMessage, monsterQueue, monsterNameReplacer, &Monster, hpRatioReplacer, hpRatio,
                          captureMessage, health, maxHealth, hpRatio);
  }
  return true;
}

void checkMonsterSize(void *monster) {
  if (!monsterChecked.contains(monster))
    return;
  bool show = true;
  int monsterId = *offsetPtr<int>(monster, 0x12280);
  float sizeModifier = *offsetPtr<float>(monster, 0x7730);
  float sizeMultiplier = *offsetPtr<float>(monster, 0x184);
  if (sizeModifier <= 0 || sizeModifier >= 2)
    sizeModifier = 1;
  float monsterSizeMultiplier = static_cast<float>(std::round(sizeMultiplier / sizeModifier * 100.0f)) / 100.0f;
  const auto &Monster = monsters[monster];
  if (Configs->EnableLogging) {
    LOG(INFO) << Monster.Name << " size check: " << monsterSizeMultiplier << " mon id: " << monsterId << "\r\n";
  }
  std::string size;
  if (monsterSizeMultiplier >= Monster.Gold) {
    size = Configs->Bigcrown;
  } else if (monsterSizeMultiplier >= Monster.Silver) {
    size = Configs->Bigsilver;
    if (Configs->OnlyDisplayGoldCrown)
      show = false;
  } else if (monsterSizeMultiplier <= Monster.Mini) {
    size = Configs->Smallcrown;
  } else {
    size = Configs->Normalsize;
    if (Configs->OnlyDisplayGoldCrown)
      show = false;
  }
  std::string re = "monstername";
  size_t pos;
  while ((pos = size.find(re)) != std::string::npos) {
    std::string capu =
        (Monster.Capture != 0)
            ? std::format("{}<STYL MOJI_BLUE_DEFAULT>({} at {}%)</STYL>", Monster.Name, "Capturable", Monster.Capture)
            : std::format("{}<STYL MOJI_WHITE_DISABLE>({})</STYL>", Monster.Name, "Uncapturable");
    size.replace(pos, re.length(), capu);
  }
  if (show)
    showMessage(size, 2);
}

bool ConfigLoading() {
  try {
    std::string nativePCPath = "nativePC";
    if (std::filesystem::exists("ICE") && std::filesystem::is_directory("ICE")) {
      nativePCPath = "ICE/ntPC";
    }
    std::ifstream file2(nativePCPath + "/plugins/HPNotification.json");
    if (file2.fail())
      return true;
    nlohmann::json ConfigFile = nlohmann::json::object();
    file2 >> ConfigFile;
    *Configs = ConfigFile.template get<HPNNS::Config>();
    if (Configs->NotificationType == "interval") {
      intervalBase = Configs->TypeValue;
      interval = intervalBase;
      CallBack = &checkHealthInterval;
    } else if (Configs->NotificationType == "HP") {
      hp = Configs->TypeValue;
      interval = 1000;
      CallBack = &checkHealthHP;
    } else if (Configs->NotificationType == "percentage") {
      percent = Configs->TypeValue;
      interval = 1000;
      CallBack = &checkHealthPerc;
    } else if (Configs->NotificationType == "queue") {
      interval = static_cast<int>(std::round(Configs->TypeValue * Configs->RatioMessages.size() / 100.0));
      CallBack = &checkHealthQueue;
    }
    if (interval < 1000)
      interval = 1000;
    for (auto &monst : Configs->Monsters) {
      const auto &language = Configs->Language;
      if (language == "cn")
        monst.Name = monst.CNName;
      else if (language == "jp")
        monst.Name = monst.JpName;
      else
        monst.Name = monst.USName;
      monst.captureMessageShown = false;
      monst.isPending = false;
      monst.prevValue = -1;
    }
    return true;
  } catch (...) {
    return true;
  }
}

void RemoveMonsterFromMemory(void *const &monster) {
  try {
    monsters.erase(monster);
  } catch (const std::exception &e) {
    LOG(INFO) << "Exception occurred while erasing monster: " << e.what() << "\r\n";
  }
  try {
    monsterMessages.erase(monster);
  } catch (const std::exception &e) {
    LOG(INFO) << "Exception occurred while erasing monster: " << e.what() << "\r\n";
  }
  try {
    monsterChecked.erase(monster);
  } catch (const std::exception &e) {
    LOG(INFO) << "Exception occurred while erasing monster: " << e.what() << "\r\n";
  }
}

#include <algorithm> // Ensure <algorithm> is included for std::max

void SingleThreadFunction(void *const monster) {
 bool validMonster = true;
 while (validMonster) {
   // Fix: Replace std::max with the correct syntax for the namespace resolution
   int validInter = max(Configs->TypeValue, 2500); // Use parentheses around std::max to avoid ambiguity
   if (Configs->EnableLogging) {
     LOG(INFO) << "Monster thread running routine interval " << validInter << "\r\n";
   }
   std::this_thread::sleep_for(std::chrono::milliseconds(validInter));
   if (!monsters.contains(monster)) {
     int id = *offsetPtr<int>(monster, 0x12280);
     const auto &list = Configs->Monsters;
     auto it = std::find_if(list.begin(), list.end(), [id](const HPNNS::Monster &mon) { return mon.Id == id; });
     if (it == list.end() || it->Name.empty() || it->Id == 0)
       return;
     monsters[monster] = *it;
     monsterMessages[monster] = Configs->RatioMessages;
     monsterChecked[monster] = {false, false};
   }
   CallBack(monster);
   auto &checked = monsterChecked[monster];
   if (checked.first && !checked.second) {
     checkMonsterSize(monster);
     checked.second = true;
   }
   validMonster = monsterChecked.contains(monster);
   if (!validMonster)
     return;
 }
 RemoveMonsterFromMemory(monster);
}

byte *get_lea_addr(byte *addr) {
  auto base_addr = addr + 7;
  auto offset = *reinterpret_cast<int *>(addr + 3);
  return base_addr + offset;
}

__declspec(dllexport) extern bool Load() {
  ConfigLoading();
  if (Configs->EnableLogging)
    LOG(INFO) << "Config loaded" << "\r\n";
  auto uenemy_ctor_addr = find_func(sig::monster_ctor);
  auto uenemy_reset_addr = find_func(sig::monster_reset);
  auto show_message_addr = find_func(sig::show_message);
  auto message_instance_addr = find_func(sig::chat_instance_source);
  if (!uenemy_ctor_addr || !show_message_addr || !message_instance_addr || !uenemy_reset_addr)
    return false;
  show_message = reinterpret_cast<decltype(show_message)>(*show_message_addr);
  chat_instance = get_lea_addr(*message_instance_addr + CHAT_INSTANCE_OFFSET);
  MH_Initialize();
  Hook<void *(void *, int, int)>::hook(*uenemy_ctor_addr, [](auto orig, auto this_, auto id, auto subid) {
    auto ret = orig(this_, id, subid);
    std::thread([id, this_]() {
      char *monsterPath = offsetPtr<char>(this_, 0x7741);
      if (monsterPath[2] == '0' || monsterPath[2] == '1') {
        std::unique_lock ul(HookListenerLok);
        if (!monsters.contains(this_)) {
          std::thread t(SingleThreadFunction, this_);
          t.detach();
          if (Configs->EnableLogging)
            LOG(INFO) << "Monster loaded: " << id << "+" << this_ << "\r\n";
        }
        if (Configs->EnableLogging)
          LOG(INFO) << "Monster created: " << id << "\r\n";
      }
    }).detach();
    return ret;
  });
  Hook<void *(void *)>::hook(*uenemy_reset_addr, [](auto orig, auto this_) {
    auto ret = orig(this_);
    std::thread([this_]() {
      if (monsterChecked.contains(this_)) {
        std::unique_lock ul(HookListenerLok);
        RemoveMonsterFromMemory(this_);
        if (Configs->EnableLogging)
          LOG(INFO) << "Monster erased" << "\r\n";
      }
    }).detach();
    return ret;
  });
  MH_ApplyQueued();
  return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
  if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
    LOG(INFO) << "DLL PROCESS ATTACHED" << "\r\n";
    return Load();
  }
  return TRUE;
}