#include <json.hpp>

namespace HPNNS {
using nlohmann::json;

#ifndef NLOHMANN_UNTYPED_q_HELPER
#define NLOHMANN_UNTYPED_q_HELPER
inline json get_untyped(const json &j, const char *property) {
  if (j.find(property) != j.end()) {
    return j.at(property).get<json>();
  }
  return json();
}

inline json get_untyped(const json &j, std::string property) { return get_untyped(j, property.data()); }
#endif

struct Monster {
  int64_t Id;
  std::string CNName;
  std::string Name;
  int64_t Capture;
  int64_t MaxHealth;
  double Mini;
  double Silver;
  double Gold;
  std::string USName;
  std::string JpName;
  double prevValue;
  bool isPending;
  bool captureMessageShown;
};

struct RatioMessage {
  double Ratio;
  std::string Msg;
};

struct Config {
  std::string Language;
  bool EnableLogging;
  std::string NotificationType;
  int64_t TypeValue;
  bool DisplayCrown;
  bool OnlyDisplayGoldCrown;
  std::string Bigcrown;
  std::string Bigsilver;
  std::string Smallcrown;
  std::string Normalsize;
  bool DisplayCapture;
  std::string Capture;
  bool DisplayHealth;
  std::string HpMessage;
  std::vector<RatioMessage> RatioMessages;
  std::vector<Monster> Monsters;
};
} // namespace HPNNS

namespace HPNNS {
void from_json(const json &j, Monster &x);
void to_json(json &j, const Monster &x);

void from_json(const json &j, RatioMessage &x);
void to_json(json &j, const RatioMessage &x);

void from_json(const json &j, Config &x);
void to_json(json &j, const Config &x);

inline void from_json(const json &j, Monster &x) {

  x.Id = j.at("Id").get<int64_t>();
  x.CNName = j.at("CNName").get<std::string>();
  x.Capture = j.at("Capture").get<int64_t>();
  x.Mini = j.at("Mini").get<double>();
  x.Silver = j.at("Silver").get<double>();
  x.Gold = j.at("Gold").get<double>();
  x.USName = j.at("USName").get<std::string>();
  x.JpName = j.at("JPName").get<std::string>();
  x.Name = j.at("USName").get<std::string>();

}

inline void to_json(json &j, const Monster &x) {
  j = json::object();
  j["Id"] = x.Id;
  j["CNName"] = x.CNName;
  j["Capture"] = x.Capture;
  j["Mini"] = x.Mini;
  j["Silver"] = x.Silver;
  j["Gold"] = x.Gold;
  j["USName"] = x.USName;
  j["JPName"] = x.JpName;
  j["Name"] = x.Name;
}

inline void from_json(const json &j, RatioMessage &x) {
  x.Ratio = j.at("ratio").get<double>();
  x.Msg = j.at("msg").get<std::string>();
}

inline void to_json(json &j, const RatioMessage &x) {
  j = json::object();
  j["ratio"] = x.Ratio;
  j["msg"] = x.Msg;
}

inline void from_json(const json &j, Config &x) {
  x.Language = j.at("Language").get<std::string>();
  x.EnableLogging = j.at("EnableLogging").get<bool>();
  x.NotificationType = j.at("NotificationType").get<std::string>();
  x.TypeValue = j.at("TypeValue").get<int64_t>();
  x.DisplayCrown = j.at("DisplayCrown").get<bool>();
  x.OnlyDisplayGoldCrown = j.at("OnlyDisplayGoldCrown").get<bool>();
  x.Bigcrown = j.at("Bigcrown").get<std::string>();
  x.Bigsilver = j.at("Bigsilver").get<std::string>();
  x.Smallcrown = j.at("Smallcrown").get<std::string>();
  x.Normalsize = j.at("Normalsize").get<std::string>();
  x.DisplayCapture = j.at("DisplayCapture").get<bool>();
  x.Capture = j.at("Capture").get<std::string>();
  x.DisplayHealth = j.at("DisplayHealth").get<bool>();
  x.HpMessage = j.at("HPMessage").get<std::string>();
  x.RatioMessages = j.at("RatioMessages").get<std::vector<RatioMessage>>();
  x.Monsters = j.at("Monsters").get<std::vector<Monster>>();
}

inline void to_json(json &j, const Config &x) {
  j = json::object();
  j["Language"] = x.Language;
  j["EnableLogging"] = x.EnableLogging;
  j["NotificationType"] = x.NotificationType;
  j["TypeValue"] = x.TypeValue;
  j["DisplayCrown"] = x.DisplayCrown;
  j["OnlyDisplayGoldCrown"] = x.OnlyDisplayGoldCrown;
  j["Bigcrown"] = x.Bigcrown;
  j["Bigsilver"] = x.Bigsilver;
  j["Smallcrown"] = x.Smallcrown;
  j["Normalsize"] = x.Normalsize;
  j["DisplayCapture"] = x.DisplayCapture;
  j["Capture"] = x.Capture;
  j["DisplayHealth"] = x.DisplayHealth;
  j["HPMessage"] = x.HpMessage;
  j["RatioMessages"] = x.RatioMessages;
  j["Monsters"] = x.Monsters;
}
} // namespace HPNNS
