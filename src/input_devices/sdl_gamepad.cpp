#include "input_devices/sdl_gamepad.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_gamecontroller.h>
#include <SDL2/SDL_joystick.h>
#include <iso646.h>

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "utils/logging.hpp"

namespace nestris_x86 {

namespace {

std::map<std::string, int> getReverseLookup(const std::map<int, std::string>& code_to_name) {
  std::map<std::string, int> name_to_code{};
  for (const auto& [id, name] : code_to_name) {
    name_to_code[name] = id;
  }
  return name_to_code;
}

class KeyCodeNameMap {
 public:
  KeyCodeNameMap() : name_to_code_{getReverseLookup(code_to_name_)} {}

  int nameToCode(const std::string& name) const { return name_to_code_.at(name); }

  std::string codeToName(const int code) const { return code_to_name_.at(code); }

  const std::map<std::string, int>& getNameToCodeMap() const { return name_to_code_; }
  const std::map<int, std::string>& getCodeToNameMap() const { return code_to_name_; }

  struct NewNameCode {
    std::string name{};
    int code{};
  };

  NewNameCode addEntry(const std::string& name_prefix) {
    const auto new_code = getUniqueCode();
    const auto new_name = getUniqueName(name_prefix);
    code_to_name_[new_code] = new_name;
    name_to_code_[new_name] = new_code;
    return {new_name, new_code};
  }

 private:
  int getUniqueCode() {
    int new_code = -1;
    while (code_to_name_.count(++new_code) != 0)
      ;
    return new_code;
  }

  std::string getUniqueName(const std::string& name_prefix) {
    int counter{};
    std::string new_name{};
    do {
      new_name = name_prefix + std::to_string(counter++);
    } while (name_to_code_.count(new_name) != 0);
    return new_name;
  }

  std::map<int, std::string> code_to_name_{
      {-1, "NONE"},    //
      {0, "FACE_D"},   //
      {1, "FACE_R"},   //
      {2, "FACE_L"},   //
      {3, "FACE_U"},   //
      {4, "L1"},       //
      {5, "R1"},       //
      {6, "START"},    //
      {7, "L3"},       //
      {8, "R3"},       //
      {10, "DPAD_L"},  //
      {11, "DPAD_R"},  //
      {12, "DPAD_U"},  //
      {13, "DPAD_D"}   //
  };
  std::map<std::string, int> name_to_code_;
};

// Maps our internal button codes to SDL Game Controller buttons.
const std::map<int, SDL_GameControllerButton> kCodeToGCButton{
    {0, SDL_CONTROLLER_BUTTON_A},
    {1, SDL_CONTROLLER_BUTTON_B},
    {2, SDL_CONTROLLER_BUTTON_X},
    {3, SDL_CONTROLLER_BUTTON_Y},
    {4, SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {5, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {6, SDL_CONTROLLER_BUTTON_START},
    {7, SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {8, SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    {10, SDL_CONTROLLER_BUTTON_DPAD_LEFT},
    {11, SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
    {12, SDL_CONTROLLER_BUTTON_DPAD_UP},
    {13, SDL_CONTROLLER_BUTTON_DPAD_DOWN},
};

// Maps axis index (as passed to registerAxisAsButton) to GC axis.
const SDL_GameControllerAxis kAxisIndexToGC[] = {
    SDL_CONTROLLER_AXIS_LEFTX,        // 0
    SDL_CONTROLLER_AXIS_LEFTY,        // 1
    SDL_CONTROLLER_AXIS_RIGHTX,       // 2
    SDL_CONTROLLER_AXIS_RIGHTY,       // 3
    SDL_CONTROLLER_AXIS_TRIGGERLEFT,  // 4
    SDL_CONTROLLER_AXIS_TRIGGERRIGHT, // 5
};
constexpr int kAxisIndexToGCSize = sizeof(kAxisIndexToGC) / sizeof(kAxisIndexToGC[0]);

}  // namespace

class SdlGamePad::Impl {
  using SdlKeyCode = int;

 public:
  Impl() : key_code_name_map_{} {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
      throw std::runtime_error("Couldn't initialize SDL: " + std::string(SDL_GetError()));
    }

    SDL_JoystickEventState(SDL_ENABLE);

    const int num = SDL_NumJoysticks();
    fprintf(stderr, "[gamepad] SDL sees %d joystick(s)\n", num);
    for (int i = 0; i < num; ++i) {
      const bool isGC = SDL_IsGameController(i);
      fprintf(stderr, "[gamepad]   [%d] %s%s\n", i, SDL_JoystickNameForIndex(i),
              isGC ? " (game controller)" : "");
    }

    tryOpen(0);

    for (const auto& [code, name] : key_code_name_map_.getCodeToNameMap()) {
      button_states_[code] = 0;
    }
  }

  ~Impl() {
    if (gc_) SDL_GameControllerClose(gc_);
    else if (joystick_) SDL_JoystickClose(joystick_);
  }

  bool getKeyState(const KeyCode key_code) {
    pollAndUpdateInteralState();
    return button_states_.at(key_code);
  }

  InputInterface::KeyCode getPressedKey() {
    pollAndUpdateInteralState();
    for (const auto& [key_code, pressed] : button_states_) {
      if (pressed) {
        return key_code;
      }
    }
    return getNullKey();
  }

  std::string keyCodeToStr(const KeyCode key_code) const {
    if (not key_code_name_map_.getCodeToNameMap().count(key_code)) {
      LOG_ERROR("Unknown key code `" << key_code << "`.");
      return std::to_string(key_code);
    }
    return key_code_name_map_.codeToName(key_code);
  }

  InputInterface::KeyCode lookupKeyCode(const std::string& key_name) const {
    if (not key_code_name_map_.getNameToCodeMap().count(key_name)) {
      LOG_ERROR("No key code found for name `" << key_name << "`.");
      return getNullKey();
    }
    return key_code_name_map_.getNameToCodeMap().at(key_name);
  }

  InputInterface::KeyCode getNullKey() const { return -1; }

  void registerAxisAsButton(const int axis_number, const double axis_at_rest,
                            const double axis_pressed) {
    const auto new_button = key_code_name_map_.addEntry("AXISB");
    button_states_[new_button.code] = false;
    axis_triggers_.push_back({axis_number, axis_at_rest, axis_pressed, new_button.code});
  }

  std::vector<InputInterface::RegisteredAxisMovement> getRegisteredAxes() const {
    std::vector<InputInterface::RegisteredAxisMovement> registered_axes;
    for (const auto& axis_trigger : axis_triggers_) {
      registered_axes.emplace_back(InputInterface::RegisteredAxisMovement{
          axis_trigger.axis_number, axis_trigger.axis_at_rest, axis_trigger.axis_pressed});
    }
    return registered_axes;
  }

 private:
  struct AxisMovementTrigger {
    int axis_number;
    double axis_at_rest;
    double axis_pressed;
    int key_code;
  };

  void tryOpen(int index) {
    if (index < 0 || index >= SDL_NumJoysticks()) return;

    if (SDL_IsGameController(index)) {
      gc_ = SDL_GameControllerOpen(index);
      if (gc_) {
        joystick_ = SDL_GameControllerGetJoystick(gc_);
        fprintf(stderr, "[gamepad] opened as game controller: %s\n",
                SDL_GameControllerName(gc_));
        return;
      }
    }
    // Fallback: raw joystick
    joystick_ = SDL_JoystickOpen(index);
    if (joystick_) {
      fprintf(stderr, "[gamepad] opened as raw joystick: %s  buttons=%d  axes=%d  hats=%d\n",
              SDL_JoystickName(joystick_),
              SDL_JoystickNumButtons(joystick_),
              SDL_JoystickNumAxes(joystick_),
              SDL_JoystickNumHats(joystick_));
    } else {
      fprintf(stderr, "[gamepad] failed to open joystick %d: %s\n", index, SDL_GetError());
    }
  }

  void processAxisTriggers() {
    for (const auto& trigger : axis_triggers_) {
      double axis_position = 0.0;
      if (gc_) {
        if (trigger.axis_number >= 0 && trigger.axis_number < kAxisIndexToGCSize) {
          axis_position = SDL_GameControllerGetAxis(gc_, kAxisIndexToGC[trigger.axis_number]);
        }
      } else {
        if (axis_states_.count(trigger.axis_number))
          axis_position = axis_states_.at(trigger.axis_number);
        else
          continue;
      }
      button_states_[trigger.key_code] = (std::abs(trigger.axis_pressed - axis_position) <
                                          std::abs(trigger.axis_at_rest - axis_position));
    }
  }

  void pollAndUpdateInteralState() {
    SDL_GameControllerUpdate();  // also updates joystick state

    // Retry opening if nothing was found at construction time (before the SDL event loop).
    if (!joystick_ && SDL_NumJoysticks() > 0) {
      tryOpen(0);
      if (joystick_) {
        for (const auto& [code, name] : key_code_name_map_.getCodeToNameMap()) {
          if (!button_states_.count(code)) button_states_[code] = false;
        }
      }
    }

    if (gc_) {
      // Game Controller API: normalized button + D-pad mapping.
      for (const auto& [code, gcBtn] : kCodeToGCButton) {
        const bool state = SDL_GameControllerGetButton(gc_, gcBtn) != 0;
        if (state != button_states_[code])
          fprintf(stderr, "[gamepad] %s: %s\n",
                  key_code_name_map_.codeToName(code).c_str(), state ? "DOWN" : "UP");
        button_states_[code] = state;
      }
    } else if (joystick_) {
      // Raw joystick fallback.
      std::set<int> axis_codes;
      for (const auto& t : axis_triggers_) axis_codes.insert(t.key_code);

      const int num_buttons = SDL_JoystickNumButtons(joystick_);
      for (const auto& [code, name] : key_code_name_map_.getCodeToNameMap()) {
        if (code >= 0 && code < num_buttons && !axis_codes.count(code)) {
          const bool state = SDL_JoystickGetButton(joystick_, code) != 0;
          if (state != button_states_[code])
            fprintf(stderr, "[gamepad] button %d (%s): %s\n", code, name.c_str(), state ? "DOWN" : "UP");
          button_states_[code] = state;
        }
      }

      if (SDL_JoystickNumHats(joystick_) > 0) {
        const auto hat = SDL_JoystickGetHat(joystick_, 0);
        button_states_[key_code_name_map_.nameToCode("DPAD_U")] = bool(hat & SDL_HAT_UP);
        button_states_[key_code_name_map_.nameToCode("DPAD_D")] = bool(hat & SDL_HAT_DOWN);
        button_states_[key_code_name_map_.nameToCode("DPAD_R")] = bool(hat & SDL_HAT_RIGHT);
        button_states_[key_code_name_map_.nameToCode("DPAD_L")] = bool(hat & SDL_HAT_LEFT);
      }

      const int num_axes = SDL_JoystickNumAxes(joystick_);
      for (int i = 0; i < num_axes; ++i) {
        axis_states_[i] = SDL_JoystickGetAxis(joystick_, i);
      }
    }

    processAxisTriggers();
  }

  KeyCodeNameMap key_code_name_map_;
  SDL_GameController* gc_ = nullptr;
  SDL_Joystick* joystick_ = nullptr;
  std::map<SdlKeyCode, bool> button_states_;
  std::map<int, double> axis_states_;
  std::vector<AxisMovementTrigger> axis_triggers_;
};

SdlGamePad::SdlGamePad() : pimpl_{std::make_unique<SdlGamePad::Impl>()} {}

SdlGamePad::~SdlGamePad() = default;

bool SdlGamePad::getKeyState(const KeyCode key_code) {
  return pimpl_->getKeyState(key_code);
}

InputInterface::KeyCode SdlGamePad::getPressedKey() {
  return pimpl_->getPressedKey();
}

std::string SdlGamePad::keyCodeToStr(const KeyCode key_code) const {
  return pimpl_->keyCodeToStr(key_code);
}

InputInterface::KeyCode SdlGamePad::lookupKeyCode(const std::string& key_name) const {
  return pimpl_->lookupKeyCode(key_name);
}

InputInterface::KeyCode SdlGamePad::getNullKey() const {
  return pimpl_->getNullKey();
}

void SdlGamePad::registerAxisAsButton(const int axis_number, const double axis_at_rest,
                                      const double axis_pressed) {
  pimpl_->registerAxisAsButton(axis_number, axis_at_rest, axis_pressed);
}

std::vector<InputInterface::RegisteredAxisMovement> SdlGamePad::getRegisteredAxes() const {
  return pimpl_->getRegisteredAxes();
}

}  // namespace nestris_x86
