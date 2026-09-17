#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/cover/cover.h"

namespace esphome {
namespace gdo {

class GdoCover : public cover::Cover, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  Trigger<> *get_single_press_trigger() { return &this->single_press_trigger_; }
  Trigger<> *get_double_press_trigger() { return &this->double_press_trigger_; }
  Trigger<> *get_triple_press_trigger() { return &this->triple_press_trigger_; }
  void set_open_endstop(binary_sensor::BinarySensor *open_endstop) { this->open_endstop_ = open_endstop; }
  void set_close_endstop(binary_sensor::BinarySensor *close_endstop) { this->close_endstop_ = close_endstop; }
  void set_open_duration(uint32_t open_duration) { this->open_duration_ = open_duration; }
  void set_close_duration(uint32_t close_duration) { this->close_duration_ = close_duration; }
  // How long double_press_action's own two-click gap is -- see the
  // CONF_DOUBLE_PRESS_SETTLE comment in cover.py.
  void set_double_press_settle(uint32_t double_press_settle) { this->double_press_settle_ = double_press_settle; }
  // Whether triple_press_action was configured at all -- see the
  // CONF_TRIPLE_PRESS_ACTION comment in cover.py. Without it, "resume the
  // same direction after a stop" falls back to double_press_trigger_.
  void set_has_triple_press_action(bool has_triple_press_action) {
    this->has_triple_press_action_ = has_triple_press_action;
  }
  void set_triple_press_settle(uint32_t triple_press_settle) { this->triple_press_settle_ = triple_press_settle; }

  cover::CoverTraits get_traits() override;

 protected:
  void control(const cover::CoverCall &call) override;
  void stop_prev_trigger_();
  bool is_at_target_() const;

  void start_direction_(cover::CoverOperation dir, bool perform_trigger = true);

  void recompute_position_();

  binary_sensor::BinarySensor *open_endstop_{nullptr};
  binary_sensor::BinarySensor *close_endstop_{nullptr};
  uint32_t open_duration_{0};
  uint32_t close_duration_{0};
  uint32_t double_press_settle_{0};
  bool has_triple_press_action_{false};
  uint32_t triple_press_settle_{0};
  // millis() deadline: recompute_position_() credits no movement before
  // this, because a double or triple press leaves the door mechanically
  // stationary for double_press_settle_ / triple_press_settle_ after the
  // command is issued (see cpp for how this is set and consumed).
  uint32_t settle_until_{0};
  Trigger<> single_press_trigger_;
  Trigger<> double_press_trigger_;
  Trigger<> triple_press_trigger_;
  Trigger<> *prev_command_trigger_{nullptr};
  uint32_t last_recompute_time_{0};
  uint32_t start_dir_time_{0};
  uint32_t last_publish_time_{0};
  float target_position_{0};

  // PATCHED: remembers which direction the door was actually moving in the
  // last time it was NOT idle, so a request from a stopped/partial position
  // can tell "reverse of last motion" (needs one press on our toggle motor)
  // apart from "same as last motion" (would need three presses, which this
  // component can't express -- see gdo_cover.cpp for details).
  cover::CoverOperation last_moving_operation_{cover::COVER_OPERATION_IDLE};
};

}  // namespace gdo
}  // namespace esphome
