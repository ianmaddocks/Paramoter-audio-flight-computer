#pragma once
#include <Arduino.h>

#include "nav/Nav.h"

// Safe flying areas: up to kMaxAreas polygons (up to kMaxPoints corners each, flat, at every height) drawn on the web page and
// kept in /areas.json. While flying the pilot is told when the track will leave the area within the warning distance or time,
// told when outside, and each time which way to turn. See docs/Feature_Reference.md.
class SafeArea {
 public:
  static constexpr int kMaxAreas = 3;
  static constexpr int kMaxPoints = 24;
  static constexpr int kNameLen = 24;
  enum class State : uint8_t { None, Inside, Edge, Outside };

  void begin();  // loads the stored areas
  // Replaces all the areas from the page's JSON {"a":[{"n":name,"on":true,"p":[[lat,lon],...]}]} and stores them. false and a
  // reason in *err when the data is not usable (nothing is changed then).
  bool setFromJson(const char* json, const char** err);
  void toJson(String& out) const;

  // Called every flight tick with the navigation state; guidanceActive = a landing approach or emergency glide is running (its
  // calls come first, so this stays quiet).
  void update(const NavState& nav, bool guidanceActive);
  void reset();  // forget the state (not the areas)

  int activeCount() const;
  State state() const { return state_; }
  float edgeM() const { return edgeM_; }  // distance to the nearest edge, in or out; -1 when unknown
  const char* stateName() const;

 private:
  struct Area {
    char name[kNameLen] = "";
    bool on = false;
    uint8_t n = 0;
    int32_t lat[kMaxPoints] = {};  // micro-degrees
    int32_t lon[kMaxPoints] = {};
  };
  Area areas_[kMaxAreas];
  bool save() const;
  static bool parse(const char* json, Area* out, const char** err);

  State state_ = State::None;
  float edgeM_ = -1;
  uint32_t lastEvalMs_ = 0;
  uint32_t lastSpokeMs_ = 0;
  uint8_t outsideCount_ = 0;
  bool wasOutside_ = false;
};
