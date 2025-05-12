// dllmain.cpp : Définit le point d'entrée de l'application DLL.
#include "MinHook.h"
#include "json.hpp"
#include "loader.h"
#include "plugin-utils.h"
#include "signatures.h"
#include "supplemental.cpp"
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
static struct HPNNS::Config *Configs = new HPNNS::Config();
static std::map<void *, std::vector<HPNNS::RatioMessage>> monsterMessages;
static std::map<void *, HPNNS::Monster> monsters;
static std::map<void *, std::pair<bool, bool>> monsterChecked;
static int intervalBase, interval, hp, percent = 0;
static std::mutex thread0;
static std::mutex thread1;
static std::mutex thread2;
const int UENEMY_LEA_VFT_OFFSET = 0x43;
const int CHAT_INSTANCE_OFFSET = 0x4d;
static void *chat_instance = nullptr;
static void (*show_message)(void *, const char *, float, unsigned int, char) = nullptr;
static bool (*CallBack)(void *monster) = nullptr;
void showMessage(std::string message) { show_message(*(void **)chat_instance, message.c_str(), -1, -1, 0); }

void showMessage(std::string message, char param3) {
  show_message(*(void **)chat_instance, message.c_str(), -1, -1, param3);
}

void ValidateAndBuildMessageString(std::string &hpMessage, std::vector<HPNNS::RatioMessage> &monsterQueue,
                                   std::string &monsterNameReplacer, HPNNS::Monster *Monster,
                                   std::string &hpRatioReplacer, float &hpRatio, std::string &captureMessage) {
  if ((*Configs).DisplayHealth) {
    hpMessage = monsterQueue.front().Msg.length() ? monsterQueue.front().Msg : (*Configs).HpMessage;
    // hpMessage = std::format("{}: {}", std::ctime(&currentTime), hpMessage);
    while (hpMessage.find(monsterNameReplacer) != std::string::npos) {
      size_t pos = hpMessage.find(monsterNameReplacer);
      hpMessage.replace(pos, monsterNameReplacer.length(), (Monster)->Name);
    }
    while (hpMessage.find(hpRatioReplacer) != std::string::npos) {
      size_t pos = hpMessage.find(hpRatioReplacer);
      hpMessage.replace(pos, hpRatioReplacer.length(), std::format("{:.2f}", hpRatio * 100.0f));
    }
  }
  if ((*Configs).DisplayCapture && (Monster)->Capture != 0 && !(Monster)->captureMessageShown) {
    if (hpRatio < (Monster)->Capture / 100.0f) {
      captureMessage = (*Configs).Capture;
      while (captureMessage.find(monsterNameReplacer) != std::string::npos) {
        size_t pos = captureMessage.find(monsterNameReplacer);
        captureMessage.replace(pos, monsterNameReplacer.length(), (Monster)->Name);
      }
      (Monster)->captureMessageShown = true;
    }
  }
}

void DisplayFinalMessage(std::string &captureMessage, std::string &hpMessage, HPNNS::Monster *Monster, float &health,
                         float &maxHealth, float &newValue) {
  if (!captureMessage.empty()) {
    showMessage(captureMessage, 2);
  }
  if (!hpMessage.empty()) {
    bool canBeCapture = (Monster)->captureMessageShown;
    if (canBeCapture) {
      hpMessage = std::format("{} Monster can be captured now.", hpMessage);
    }
    hpMessage = std::format("{}\r\nHP: <STYL MOJI_GREEN_DEFAULT>{:.0f}</STYL>/<STYL MOJI_GREEN_DEFAULT>{:.0f}</STYL>",
                            hpMessage, health, maxHealth);

    showMessage(hpMessage, 2);
    (Monster)->prevValue = newValue;
  }
}

int BuildMonsterInformation(void *&monster, HPNNS::Monster *Monster, float &health, float &maxHealth,
                            std::vector<HPNNS::RatioMessage> &monsterQueue, float &hpRatio) {
  bool validMonster = monsterMessages.contains(monster);
  if (!validMonster)
    return -1;
  void *healthMgr = *offsetPtr<void *>(monster, 0x7670);
  health = *offsetPtr<float>(healthMgr, 0x64);
  maxHealth = *offsetPtr<float>(healthMgr, 0x60);
  monsterQueue = monsterMessages[monster];
  if (Monster->isPending) {
    return 0;
  }

  // if (health == 100 && maxHealth == 100) {
  //   Monster->MaxHealth = -1;
  //   return 0;
  // }
  hpRatio = (health / maxHealth);
  if (hpRatio <= 0.05f) {

    return -1;
  }
  std::string hpText = std::format("{} : {:.2f}/{:.2f} ({:.2f}%)", Monster->Name, health, maxHealth, hpRatio);
  return 1;
}

void ProcessDisplayMessage(std::string &hpMessage, std::vector<HPNNS::RatioMessage> &monsterQueue,
                           std::string &monsterNameReplacer, HPNNS::Monster *Monster, std::string &hpRatioReplacer,
                           float &hpRatio, std::string &captureMessage, float &health, float &maxHealth,
                           float &newValue) {

  ValidateAndBuildMessageString(hpMessage, monsterQueue, monsterNameReplacer, &*Monster, hpRatioReplacer, hpRatio,
                                captureMessage);

  DisplayFinalMessage(captureMessage, hpMessage, &*Monster, health, maxHealth, newValue);
}

void RevalidateMaxHP(HPNNS::Monster *Monster, float maxHealth, void *&monster) {
  int intMaxHealth = round(maxHealth);
  if (Monster->MaxHealth != intMaxHealth) {
    monsterMessages[monster] = Configs->RatioMessages;
    Monster->MaxHealth = intMaxHealth;
    monsterChecked[monster].first = true;
    (Monster)->prevValue = -1;
  }
}

 bool checkHealthQueue(void *monster) {
  HPNNS::Monster &Monster = monsters[monster];
  std::vector<HPNNS::RatioMessage> &monsterQueue(Configs->RatioMessages);

  float maxHealth, health, hpRatio;
  std::string hpMessage;
  std::string captureMessage;
  std::string monsterNameReplacer = "monstername";
  std::string hpRatioReplacer = "hpratio";

  int result = BuildMonsterInformation(monster, &Monster, health, maxHealth, monsterQueue, hpRatio);
  if (result == -1)
    return false;

  if (monsterQueue.empty()) {
    return false;
  }

  if (!(health == 100 && maxHealth == 100)) {
    RevalidateMaxHP(&Monster, maxHealth, monster);
  }

  while (!monsterQueue.empty() && result && hpRatio != 1.0f && hpRatio < monsterQueue.front().Ratio) {
    result = monsterMessages.contains(monster);
    if (!result) {
      return false;
    }
    (Monster).isPending = true;
    ValidateAndBuildMessageString(hpMessage, monsterQueue, monsterNameReplacer, &Monster, hpRatioReplacer, hpRatio,
                                  captureMessage);
    monsterQueue.erase(monsterQueue.begin());
    (Monster).isPending = false;
  }
  DisplayFinalMessage(captureMessage, hpMessage, &Monster, health, maxHealth, hpRatio);

  return true;
}

bool checkHealthInterval(void *monster) {
  HPNNS::Monster &Monster = monsters[monster];
  std::vector<HPNNS::RatioMessage> &monsterQueue(Configs->RatioMessages);

  float maxHealth, health, hpRatio;
  std::string hpMessage;
  std::string captureMessage;
  std::string monsterNameReplacer = "monstername";
  std::string hpRatioReplacer = "hpratio";

  int result = BuildMonsterInformation(monster, &Monster, health, maxHealth, monsterQueue, hpRatio);
  if (result == -1)
    return false;

  if (!(health == 100 && maxHealth == 100)) {
    RevalidateMaxHP(&Monster, maxHealth, monster);
  }

  (Monster).prevValue = (Monster).prevValue == -1 ? 1 : (Monster).prevValue;

  if (result && hpRatio < (Monster).prevValue) {
    ProcessDisplayMessage(hpMessage, monsterQueue, monsterNameReplacer, &Monster, hpRatioReplacer, hpRatio,
                          captureMessage, health, maxHealth, hpRatio);
  }
  (Monster).isPending = false;
  // monsters[monster] = Monster;

  return true;
}

 bool checkHealthHP(void *monster) {
  HPNNS::Monster &Monster = monsters[monster];
  std::vector<HPNNS::RatioMessage> &monsterQueue(Configs->RatioMessages);

  float maxHealth, health, hpRatio;
  std::string hpMessage;
  std::string captureMessage;
  std::string monsterNameReplacer = "monstername";
  std::string hpRatioReplacer = "hpratio";

  int result = BuildMonsterInformation(monster, &Monster, health, maxHealth, monsterQueue, hpRatio);
  if (result == -1)
    return false;

  if (!(health == 100 && maxHealth == 100)) {
    RevalidateMaxHP(&Monster, maxHealth, monster);
  }
  (Monster).prevValue = (Monster).prevValue == -1 ? health : (Monster).prevValue;

  if (health < (Monster).prevValue && ((Monster).prevValue - health) > hp) {
    ProcessDisplayMessage(hpMessage, monsterQueue, monsterNameReplacer, &Monster, hpRatioReplacer, hpRatio,
                          captureMessage, health, maxHealth, health);
  }

  return true;
}

 bool checkHealthPerc(void *monster) {
  HPNNS::Monster &Monster = monsters[monster];
  std::vector<HPNNS::RatioMessage> &monsterQueue(Configs->RatioMessages);

  float maxHealth, health, hpRatio;
  std::string hpMessage;
  std::string captureMessage;
  std::string monsterNameReplacer = "monstername";
  std::string hpRatioReplacer = "hpratio";

  int result = BuildMonsterInformation(monster, &Monster, health, maxHealth, monsterQueue, hpRatio);
  if (result == -1)
    return false;

  if (!(health == 100 && maxHealth == 100)) {
    RevalidateMaxHP(&Monster, maxHealth, monster);
  }
  (Monster).prevValue = (Monster).prevValue == -1 ? 1 : (Monster).prevValue;

  if (hpRatio <= (Monster).prevValue && ((Monster).prevValue - hpRatio) > percent) {
    ProcessDisplayMessage(hpMessage, monsterQueue, monsterNameReplacer, &Monster, hpRatioReplacer, hpRatio,
                          captureMessage, health, maxHealth, hpRatio);
  }

  return true;
}

void checkMonsterSize(void *monster) {
  if (monsterChecked.contains(monster)) {

    bool show = true;
    int monsterId = *offsetPtr<int>(monster, 0x12280);
    float sizeModifier = *offsetPtr<float>(monster, 0x7730);
    float sizeMultiplier = *offsetPtr<float>(monster, 0x184);
    if (sizeModifier <= 0 || sizeModifier >= 2) {
      sizeModifier = 1;
    }
    float monsterSizeMultiplier = static_cast<float>(std::round(sizeMultiplier / sizeModifier * 100.0f)) / 100.0f;
    HPNNS::Monster Monster = monsters[monster];

    std::string size;
    if (monsterSizeMultiplier >= Monster.Gold) {
      size = (*Configs).Bigcrown;
    } else if (monsterSizeMultiplier >= Monster.Silver) {
      size = (*Configs).Bigsilver;
      if ((*Configs).OnlyDisplayGoldCrown) {
        show = false;
      }
    } else if (monsterSizeMultiplier <= Monster.Mini) {
      size = (*Configs).Smallcrown;
    } else {
      size = (*Configs).Normalsize;
      if ((*Configs).OnlyDisplayGoldCrown) {
        show = false;
      }
    }

    std::string re = "monstername";
    while (size.find(re) != std::string::npos) {
      size_t pos = size.find(re);
      std::string capu;
      if (Monster.Capture != 0) {
        capu = "Capturable";
        size.replace(pos, re.length(),
                     format("{}<STYL MOJI_BLUE_DEFAULT>({} at {}%)</STYL>", Monster.Name, capu, Monster.Capture));
      } else {
        capu = "Uncapturable";
        size.replace(pos, re.length(), format("{}<STYL MOJI_BLACK_DEFAULT>({})</STYL>", Monster.Name, capu));
      }
    }
    if (show) {
      showMessage(size, 2);
    }
  }
}

bool ConfigLoading() {
  try {
    std::string nativePCPath = "nativePC";
    // check if using ICE mod
    if (std::filesystem::exists("ICE")) {
      if (std::filesystem::is_directory("ICE")) {
        nativePCPath = "ICE/ntPC";
      }
    }
    std::ifstream file2(nativePCPath.append("/plugins/HPNotification.json"));
    if (file2.fail()) {
      return true;
    }
    nlohmann::json ConfigFile = nlohmann::json::object();
    file2 >> ConfigFile;
    (*Configs) = ConfigFile.template get<HPNNS::Config>();

    if ((*Configs).NotificationType._Equal("interval")) {
      intervalBase = (*Configs).TypeValue;
      interval = intervalBase;
      CallBack = &checkHealthInterval;
    }
    if ((*Configs).NotificationType._Equal("HP")) {
      hp = (*Configs).TypeValue;
      interval = 1000;
      CallBack = &checkHealthHP;
    }
    if ((*Configs).NotificationType._Equal("percentage")) {
      percent = (*Configs).TypeValue;
      interval = 1000;
      CallBack = &checkHealthPerc;
    }
    if ((*Configs).NotificationType._Equal("queue")) {
      interval = round((*Configs).TypeValue * (*Configs).RatioMessages.size() / 100);
      CallBack = &checkHealthQueue;
    }
    if (interval < 1000) {
      interval = 1000;
    }
    for (HPNNS::Monster &monst : (*Configs).Monsters) {
      std::string language = (*Configs).Language;
      monst.Name = monst.USName;
      if (language == "cn") {
        monst.Name = monst.CNName;
      }
      if (language == "jp") {
        monst.Name = monst.JpName;
      }
      monst.captureMessageShown = false;
      monst.isPending = false;
      monst.prevValue = -1;
    }
    return true;
  } catch (int errCode) {

    return true;
  }
}

std::mutex slthreadLock;
void SingleThreadFunction(void *const monster) {
  bool validMonster = true;

  while (validMonster) {
    int validInter = round((*Configs).TypeValue * (*Configs).RatioMessages.size() / 100);
    validInter = validInter < 2000 ? 2000 : validInter;
    {
      std::unique_lock ul(slthreadLock);
      if (!monsters.contains(monster) && monsterChecked.contains(monster)) {
        int monsterId = *offsetPtr<int>(monster, 0x12280);
        auto list = Configs->Monsters;
        const auto it = std::find_if(list.begin(), list.end(),
                                     [monsterId](const HPNNS::Monster &mon) { return mon.Id == monsterId; });
        auto mon = *it;
        monsters[monster] = mon;
      }
      
      CallBack(monster);
      auto &checked = monsterChecked[monster];
      if (checked.first && !checked.second) {
        checkMonsterSize(monster);
        checked.second = true;
      }
      validMonster = monsterMessages.contains(monster);
      if (!validMonster) {
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(validInter));
  }
}

byte *get_lea_addr(byte *addr) {
  auto base_addr = addr + 7; // size of call instruction
  auto offset = *reinterpret_cast<int *>(addr + 3);
  return base_addr + offset;
}

static std::mutex HookListenerLok;
void handleMonsterCreated(int id, void *monster) {
  char *monsterPath = offsetPtr<char>(monster, 0x7741);
  if (monsterPath[2] == '0' || monsterPath[2] == '1') {
    std::unique_lock<std::mutex> ul(HookListenerLok);
    bool crownDisplay = false;
    monsterMessages[monster] = (*Configs).RatioMessages;
    monsterChecked[monster] = {false, false};
  }
}
static std::map<void *, std::thread::id> monsterThread;
static std::mutex Sizelock;
static std::mutex Removelock;
__declspec(dllexport) extern bool Load() {
  ConfigLoading();

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
    handleMonsterCreated(id, this_);
    return ret;
  });

  // Hook to enemy reset
  Hook<void *(void *)>::hook(*uenemy_reset_addr, [](auto orig, auto this_) {
    {
      std::unique_lock ul(Removelock);
      monsterMessages.erase(this_);
      monsterChecked.erase(this_);
      monsters.erase(this_);
      monsterThread.erase(this_);
    }
    return orig(this_);
  });

  std::thread([]() {
    while (true) {
      {
        std::unique_lock ul(Sizelock);
        for (auto [monster, checked] : monsterChecked) {
          if (!monsterThread.contains(monster)) {
            std::thread t(SingleThreadFunction, monster);
            t.detach();
            monsterThread[monster] = t.get_id();
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }).detach();

   

  MH_ApplyQueued();

  return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
  if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
    return Load();
  }

  return TRUE;
}