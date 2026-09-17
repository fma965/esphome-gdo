#include "gdo_cover.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace gdo {

static const char *const TAG = "gdo.cover";

const float UNKNOWN_POSITION = 0.5f;
// While an endstop is configured but has not confirmed the travel yet, the
// time-based estimate is held just short of the limit. Reporting a flat 100%
// (or 0%) makes the cover look finished while the door is still moving.
const float ALMOST_OPEN = 0.99f;
const float ALMOST_CLOSED = 0.01f;

using namespace esphome::cover;

// How long to wait for an endstop before giving up on it. The travel-time
// estimate is never exact, so allow a margin over the configured duration.
// Timing out at exactly the duration means a door that takes 12.8s when
// configured for 12.6s glitches to an unknown position on every single cycle.
static uint32_t endstop_timeout(uint32_t duration) { return duration + duration / 4 + 2000; }

void GdoCover::dump_config() {
  LOG_COVER("", "GDO Cover", this);
  LOG_BINARY_SENSOR("  ", "Open Endstop", this->open_endstop_);
  ESP_LOGCONFIG(TAG, "  Open Duration: %.1fs", this->open_duration_ / 1e3f);
  LOG_BINARY_SENSOR("  ", "Close Endstop", this->close_endstop_);
  ESP_LOGCONFIG(TAG, "  Close Duration: %.1fs", this->close_duration_ / 1e3f);
}

void GdoCover::setup() {
  // Restore state after restart
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    this->position = UNKNOWN_POSITION;
  }
  if (this->open_endstop_ != nullptr && this->open_endstop_->has_state()) {
    // Fix restored state based on open endstop
    if (this->open_endstop_->state) {
      this->position = COVER_OPEN;
    } else if (this->position == COVER_OPEN) {
      this->position = UNKNOWN_POSITION;
    }
  }
  if (this->close_endstop_ != nullptr && this->close_endstop_->has_state()) {
    // Fix restored state based on closed endstop
    if (this->close_endstop_->state) {
      this->position = COVER_CLOSED;
    } else if (this->position == COVER_CLOSED) {
      this->position = UNKNOWN_POSITION;
    }
  }
  if (this->open_endstop_ != nullptr) {
    this->open_endstop_->add_on_state_callback([this](bool value) {
      if (value) {
        // Reached the open endstop. Update state
        float dur = (millis() - this->start_dir_time_) / 1e3f;
        ESP_LOGI(TAG, "Open endstop reached. Took %.1fs.", dur);
        // PATCHED: remember we were opening before we drop to idle below.
        if (this->current_operation != COVER_OPERATION_IDLE) {
          this->last_moving_operation_ = this->current_operation;
        }
        this->position = COVER_OPEN;
        this->target_position_ = COVER_OPEN;
        this->current_operation = COVER_OPERATION_IDLE;
        this->publish_state();
      } else {
        // Moved away from the open endstop.
        // If this was triggered by an external control assume target position is fully closed
        // and start updating state without triggering a press.
        ESP_LOGI(TAG, "Open endstop released.");
        if (this->current_operation == COVER_OPERATION_IDLE) {
          this->target_position_ = COVER_CLOSED;
          this->start_direction_(COVER_OPERATION_CLOSING, false);
        }
      }
    });
  }
  if (this->close_endstop_ != nullptr) {
    this->close_endstop_->add_on_state_callback([this](bool value) {
      if (value) {
        // Reached the closed endstop. Update state
        float dur = (millis() - this->start_dir_time_) / 1e3f;
        ESP_LOGI(TAG, "Closed endstop reached. Took %.1fs.", dur);
        // PATCHED: remember we were closing before we drop to idle below.
        if (this->current_operation != COVER_OPERATION_IDLE) {
          this->last_moving_operation_ = this->current_operation;
        }
        this->position = COVER_CLOSED;
        this->target_position_ = COVER_CLOSED;
        this->current_operation = COVER_OPERATION_IDLE;
        this->publish_state();
      } else {
        // Moved away from the closed endstop.
        // If this was triggered by an external control assume target position is fully open
        // and start updating state without triggering a press.
        ESP_LOGI(TAG, "Closed endstop released.");
        if (this->current_operation == COVER_OPERATION_IDLE) {
          this->target_position_ = COVER_OPEN;
          this->start_direction_(COVER_OPERATION_OPENING, false);
        }
      }
    });
  }
}

void GdoCover::loop() {
  if (this->current_operation == COVER_OPERATION_IDLE) {
    return;
  }

  const uint32_t now = millis();

  // Recompute position every loop cycle
  this->recompute_position_();

  if (this->is_at_target_()) {
    if (this->target_position_ == COVER_OPEN || this->target_position_ == COVER_CLOSED) {
      // Don't trigger stop, let the cover stop by itself.
      this->current_operation = COVER_OPERATION_IDLE;
    } else {
      this->start_direction_(COVER_OPERATION_IDLE);
    }
    this->publish_state();
  } else if ((this->current_operation == COVER_OPERATION_OPENING && this->open_endstop_ != nullptr &&
              now - this->start_dir_time_ > endstop_timeout(this->open_duration_)) ||
             (this->current_operation == COVER_OPERATION_CLOSING && this->close_endstop_ != nullptr &&
              now - this->start_dir_time_ > endstop_timeout(this->close_duration_))) {
    ESP_LOGI(TAG, "Failed to reach endstop. Likely stopped externally.");
    this->position = UNKNOWN_POSITION;
    this->current_operation = COVER_OPERATION_IDLE;
    this->publish_state();
  }

  // Send current position every second
  if (now - this->last_publish_time_ > 1000) {
    this->publish_state(false);
    this->last_publish_time_ = now;
  }
}

float GdoCover::get_setup_priority() const { return setup_priority::DATA; }

CoverTraits GdoCover::get_traits() {
  auto traits = CoverTraits();
  traits.set_supports_stop(true);
  traits.set_supports_position(true);
  return traits;
}

void GdoCover::control(const CoverCall &call) {
  if (call.get_stop()) {
    this->start_direction_(COVER_OPERATION_IDLE);
    this->publish_state();
  }
  if (call.get_position().has_value()) {
    auto pos = *call.get_position();
    if (pos == this->position) {
      ESP_LOGI(TAG, "Nothing to do. Already at target position.");
    } else {
      auto op = pos < this->position ? COVER_OPERATION_CLOSING : COVER_OPERATION_OPENING;
      this->target_position_ = pos;
      this->start_direction_(op);
    }
  }
}

void GdoCover::stop_prev_trigger_() {
  if (this->prev_command_trigger_ != nullptr) {
    this->prev_command_trigger_->stop_action();
    this->prev_command_trigger_ = nullptr;
  }
}

bool GdoCover::is_at_target_() const {
  switch (this->current_operation) {
    case COVER_OPERATION_OPENING:
      if (this->target_position_ == COVER_OPEN && this->open_endstop_ != nullptr) {
        return this->open_endstop_->state;
      }
      return this->position >= this->target_position_;
    case COVER_OPERATION_CLOSING:
      if (this->target_position_ == COVER_CLOSED && this->close_endstop_ != nullptr) {
        return this->close_endstop_->state;
      }
      return this->position <= this->target_position_;
    case COVER_OPERATION_IDLE:
    default:
      return true;
  }
}

void GdoCover::start_direction_(CoverOperation dir, bool perform_trigger) {
  if (dir == this->current_operation) {
    ESP_LOGI(TAG, "Nothing to do. CoverOperation %d didn't change.", dir);
    return;
  }

  // PATCHED: remember the direction we're actually leaving, before we
  // overwrite current_operation below. This is what lets the IDLE/partial
  // branches further down tell "reverse of last motion" (one press, on our
  // stop-then-reverse toggle motor) apart from "same as last motion" (see
  // note below -- that case needs three presses, which this component's
  // single/double press model can't express).
  if (this->current_operation != COVER_OPERATION_IDLE) {
    this->last_moving_operation_ = this->current_operation;
  }

  this->recompute_position_();
  Trigger<> *trig;
  switch (dir) {
    case COVER_OPERATION_IDLE:
      switch (this->current_operation) {
        case COVER_OPERATION_OPENING:
          // Confirmed motor behavior: a single press always stops the door,
          // opening or closing.
          ESP_LOGI(TAG, "Door is opening. Asked to stop.");
          trig = &this->single_press_trigger_;
          break;
        case COVER_OPERATION_CLOSING:
          // PATCHED: was double_press_trigger_. On our toggle motor a single
          // press stops the door in EITHER direction -- the previous double
          // press here stopped it and then immediately fired it back off
          // again (the classic "stop while closing re-opens" symptom).
          ESP_LOGI(TAG, "Door is closing. Asked to stop.");
          trig = &this->single_press_trigger_;
          break;
        default:
          return;
      }
      break;
    case COVER_OPERATION_OPENING:
      switch (this->current_operation) {
        case COVER_OPERATION_IDLE:
          if (this->position == COVER_CLOSED) {
            ESP_LOGI(TAG, "Door is fully closed. Asked to open.");
            trig = &this->single_press_trigger_;
          } else if (this->position == COVER_OPEN) {
            ESP_LOGW(TAG, "Door is fully open. Cannot open more.");
            return;
          } else if (this->last_moving_operation_ == COVER_OPERATION_CLOSING) {
            // PATCHED: stopped mid-close, now asked to open -- that's a
            // reversal, one press does it.
            ESP_LOGI(TAG, "Door is partially open (was closing). Asked to open: single press.");
            trig = &this->single_press_trigger_;
          } else {
            // PATCHED: stopped mid-open, now asked to open (i.e. "keep
            // going the way it was already going"). On a motor where the
            // very next press always reverses, resuming the SAME direction
            // actually needs stop -> reverse -> stop -> reverse, i.e. three
            // presses. This component only has single/double press actions,
            // so this specific case can't be done cleanly -- double press
            // is the closest available approximation and will look like a
            // brief flinch the wrong way before settling. If you hit this
            // in practice, say so and we can add a third press stage.
            ESP_LOGW(TAG, "Door is partially open (was opening). Asked to open more: "
                          "this toggle motor can't resume the same direction after a "
                          "stop without a 3rd press -- expect a brief flinch.");
            trig = &this->double_press_trigger_;
          }
          break;
        case COVER_OPERATION_CLOSING:
          // PATCHED: was single_press_trigger_. On our motor a single press
          // while closing only STOPS it -- it takes a second, separate
          // press to then reverse into opening. Double press (stop, then
          // go) is what actually gets from closing to opening.
          ESP_LOGI(TAG, "Door is closing. Asked to open.");
          trig = &this->double_press_trigger_;
          break;
        default:
          return;
      }
      break;
    case COVER_OPERATION_CLOSING:
      switch (this->current_operation) {
        case COVER_OPERATION_IDLE:
          if (this->position == COVER_CLOSED) {
            ESP_LOGI(TAG, "Door is fully closed. Cannot close more.");
            return;
          } else if (this->position == COVER_OPEN) {
            ESP_LOGI(TAG, "Door is fully open. Asked to close.");
            trig = &this->single_press_trigger_;
          } else if (this->last_moving_operation_ == COVER_OPERATION_OPENING) {
            // PATCHED: stopped mid-open, now asked to close -- a reversal,
            // one press does it.
            ESP_LOGI(TAG, "Door is partially open (was opening). Asked to close: single press.");
            trig = &this->single_press_trigger_;
          } else {
            // PATCHED: same "resume the same direction" limitation as
            // above, mirrored for closing.
            ESP_LOGW(TAG, "Door is partially open (was closing). Asked to close more: "
                          "this toggle motor can't resume the same direction after a "
                          "stop without a 3rd press -- expect a brief flinch.");
            trig = &this->double_press_trigger_;
          }
          break;
        case COVER_OPERATION_OPENING:
          // Already correct: single press while opening only stops it, so
          // getting all the way to closing needs stop-then-go (double).
          ESP_LOGI(TAG, "Door is opening. Asked to close.");
          trig = &this->double_press_trigger_;
          break;
        default:
          return;
      }
      break;
    default:
      return;
  }

  this->current_operation = dir;

  const uint32_t now = millis();
  this->start_dir_time_ = now;
  this->last_recompute_time_ = now;

  if (perform_trigger) {
    this->stop_prev_trigger_();
    trig->trigger();
    this->prev_command_trigger_ = trig;
  }
}

void GdoCover::recompute_position_() {
  float dir;
  float action_dur;
  float min_pos = COVER_CLOSED;
  float max_pos = COVER_OPEN;
  switch (this->current_operation) {
    case COVER_OPERATION_OPENING:
      dir = 1.0f;
      action_dur = this->open_duration_;
      // Only claim fully open once the endstop says so. Gate on the target
      // being COVER_OPEN, so a partial target of e.g. 0.995 stays reachable.
      if (this->open_endstop_ != nullptr && this->target_position_ == COVER_OPEN && !this->open_endstop_->state) {
        max_pos = ALMOST_OPEN;
      }
      break;
    case COVER_OPERATION_CLOSING:
      dir = -1.0f;
      action_dur = this->close_duration_;
      if (this->close_endstop_ != nullptr && this->target_position_ == COVER_CLOSED && !this->close_endstop_->state) {
        min_pos = ALMOST_CLOSED;
      }
      break;
    case COVER_OPERATION_IDLE:
    default:
      return;
  }
  const uint32_t now = millis();
  this->position += dir * (now - this->last_recompute_time_) / action_dur;
  this->position = clamp(this->position, min_pos, max_pos);
  this->last_recompute_time_ = now;
}

}  // namespace gdo
}  // namespace esphome
