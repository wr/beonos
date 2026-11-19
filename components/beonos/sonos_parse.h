// Beonos: parsing of Sonos UPnP replies and speaker choice.
//
// Pure functions with no ESPHome or ESP-IDF dependencies, so they can be
// unit-tested on a computer (see tests/). Everything works on plain
// std::string scanning; no XML library.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <vector>

namespace beonos {
namespace sonos {

// The search target every Sonos speaker answers to.
extern const char *const ZONE_PLAYER_ST;

struct SsdpReply {
  std::string location;  // URL of the speaker's device description
  std::string usn;
};

// Parses one SSDP M-SEARCH reply. Accepts only replies whose ST is the Sonos
// ZonePlayer device type.
bool parse_ssdp_reply(const std::string &packet, SsdpReply &out);

struct DeviceInfo {
  std::string uuid;  // RINCON_...
  std::string room;
  std::string model;
  bool has_line_in{false};  // the speaker offers the AudioIn service
};

// Parses /xml/device_description.xml.
bool parse_device_description(const std::string &xml, DeviceInfo &out);

struct Member {
  std::string uuid;
  std::string room;
  std::string ip;
  bool invisible{false};     // hidden half of a stereo pair (or another hidden member)
  bool ht_satellite{false};  // surround or sub bonded to a home-theatre primary
};

struct Group {
  std::string coordinator_uuid;
  std::vector<Member> members;
};

// Parses a ZoneGroupTopology#GetZoneGroupState SOAP response.
bool parse_zone_group_topology(const std::string &soap, std::vector<Group> &out);

struct Candidate {
  std::string uuid;
  std::string room;
  std::string ip;
};

// Speakers the turntable could be plugged into: visible members with a
// line-in. Hidden pair members and home-theatre satellites are left out, so a
// stereo pair is one entry. Sorted by room name.
std::vector<Candidate> line_in_candidates(const std::vector<Group> &groups,
                                          const std::map<std::string, DeviceInfo> &devices);

struct Target {
  std::string control_ip;  // coordinator of the group the chosen speaker is in
  std::string line_in_uuid;
};

// Finds where commands for the chosen speaker go.
bool resolve_target(const std::vector<Group> &groups, const std::string &chosen_uuid, Target &out);

enum class Choice {
  KEEP_SAVED,   // a speaker was already chosen; keep it even if it didn't answer this time
  AUTO_PICKED,  // nothing chosen yet and exactly one candidate
  NEEDS_USER,   // nothing chosen yet and more than one candidate
  NONE_FOUND,   // nothing chosen yet and no candidates
};

Choice choose_speaker(const std::vector<Candidate> &candidates, const std::string &saved_uuid,
                      std::string &out_uuid);

bool parse_transport_state(const std::string &soap, std::string &state);
bool parse_current_uri(const std::string &soap, std::string &uri);
bool is_line_in_uri(const std::string &uri);
// The RINCON id in an x-rincon-stream: URI, or "" when it isn't one.
std::string line_in_source(const std::string &uri);

std::string host_from_url(const std::string &url);
std::string xml_unescape(const std::string &s);

}  // namespace sonos
}  // namespace beonos
