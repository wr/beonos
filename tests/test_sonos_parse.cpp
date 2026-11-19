// Host tests for components/beonos/sonos_parse.cpp. Run with tests/run.sh.
//
// The XML below is hand-written to match the shape of what Sonos speakers
// return; addresses and ids are made up.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sonos_parse.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace beonos::sonos;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    ++g_checks;                                                        \
    if (!(cond)) {                                                     \
      ++g_failures;                                                    \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                  \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

// ── Fixtures ────────────────────────────────────────────────────────────────

static const char *SSDP_ZONEPLAYER =
    "HTTP/1.1 200 OK\r\n"
    "CACHE-CONTROL: max-age = 1800\r\n"
    "EXT:\r\n"
    "location: http://192.168.1.20:1400/xml/device_description.xml\r\n"
    "SERVER: Linux UPnP/1.0 Sonos/80.1-1234 (ZPS6)\r\n"
    "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
    "USN: uuid:RINCON_AAAAAAAAAAAA01400::urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
    "\r\n";

static const char *SSDP_OTHER =
    "HTTP/1.1 200 OK\r\n"
    "LOCATION: http://192.168.1.5:80/desc.xml\r\n"
    "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
    "USN: uuid:gateway\r\n"
    "\r\n";

static std::string device_xml(const std::string &uuid, const std::string &room, const std::string &model,
                              bool line_in, int sw_gen = 0) {
  std::string audio_in =
      line_in ? "<service><serviceType>urn:schemas-upnp-org:service:AudioIn:1</serviceType>"
                "<serviceId>urn:upnp-org:serviceId:AudioIn</serviceId></service>"
              : "";
  return "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
         "<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major></specVersion>"
         "<device><deviceType>urn:schemas-upnp-org:device:ZonePlayer:1</deviceType>" +
         (sw_gen ? "<swGen>" + std::to_string(sw_gen) + "</swGen>" : std::string()) +
         "<friendlyName>x</friendlyName>"
         "<friendlyName>192.168.1.20 - " +
         model + "</friendlyName><manufacturer>Sonos, Inc.</manufacturer><modelName>" + model +
         "</modelName><UDN>uuid:" + uuid + "</UDN><roomName>" + room +
         "</roomName><serviceList>"
         "<service><serviceType>urn:schemas-upnp-org:service:AlarmClock:1</serviceType></service>" +
         audio_in +
         "<service><serviceType>urn:schemas-upnp-org:service:ZoneGroupTopology:1</serviceType></service>"
         "</serviceList><deviceList><device><deviceType>urn:schemas-upnp-org:device:MediaRenderer:1"
         "</deviceType><UDN>uuid:" +
         uuid + "_MR</UDN></device></deviceList></device></root>";
}

// Escapes the inner topology the way it travels inside the SOAP body.
static std::string soap_escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '&': out += "&amp;"; break;
      default: out += c;
    }
  }
  return out;
}

static std::string topology_soap(const std::string &zone_groups) {
  std::string inner = "<ZoneGroupState><ZoneGroups>" + zone_groups +
                      "</ZoneGroups><VanishedDevices></VanishedDevices></ZoneGroupState>";
  return "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
         "<u:GetZoneGroupStateResponse xmlns:u=\"urn:schemas-upnp-org:service:ZoneGroupTopology:1\">"
         "<ZoneGroupState>" +
         soap_escape(inner) + "</ZoneGroupState></u:GetZoneGroupStateResponse></s:Body></s:Envelope>";
}

static std::string member(const std::string &uuid, const std::string &ip, const std::string &room,
                          const std::string &extra = "", const std::string &children = "") {
  std::string attrs = "UUID=\"" + uuid + "\" Location=\"http://" + ip +
                      ":1400/xml/device_description.xml\" ZoneName=\"" + room + "\" SoftwareVersion=\"80.1\"" +
                      (extra.empty() ? "" : " " + extra);
  if (children.empty())
    return "<ZoneGroupMember " + attrs + "/>";
  return "<ZoneGroupMember " + attrs + ">" + children + "</ZoneGroupMember>";
}

static std::string satellite(const std::string &uuid, const std::string &ip, const std::string &room) {
  return "<Satellite UUID=\"" + uuid + "\" Location=\"http://" + ip +
         ":1400/xml/device_description.xml\" ZoneName=\"" + room + "\" Invisible=\"1\"/>";
}

static std::string group(const std::string &coordinator, const std::string &members) {
  return "<ZoneGroup Coordinator=\"" + coordinator + "\" ID=\"" + coordinator + ":42\">" + members +
         "</ZoneGroup>";
}

static std::string transport_soap(const std::string &state) {
  return "<s:Envelope><s:Body><u:GetTransportInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
         "<CurrentTransportState>" +
         state +
         "</CurrentTransportState><CurrentTransportStatus>OK</CurrentTransportStatus>"
         "<CurrentSpeed>1</CurrentSpeed></u:GetTransportInfoResponse></s:Body></s:Envelope>";
}

static std::string media_soap(const std::string &uri) {
  return "<s:Envelope><s:Body><u:GetMediaInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
         "<NrTracks>1</NrTracks><CurrentURI>" +
         uri + "</CurrentURI><CurrentURIMetaData></CurrentURIMetaData></u:GetMediaInfoResponse></s:Body></s:Envelope>";
}

static DeviceInfo dev(const std::string &uuid, const std::string &room, bool line_in) {
  DeviceInfo d;
  d.uuid = uuid;
  d.room = room;
  d.has_line_in = line_in;
  return d;
}

// ── Tests ───────────────────────────────────────────────────────────────────

static void test_ssdp_accepts_zoneplayer() {
  SsdpReply r;
  CHECK(parse_ssdp_reply(SSDP_ZONEPLAYER, r));
  CHECK_EQ(r.location, "http://192.168.1.20:1400/xml/device_description.xml");
  CHECK_EQ(host_from_url(r.location), "192.168.1.20");
}

static void test_ssdp_rejects_other_st() {
  SsdpReply r;
  CHECK(!parse_ssdp_reply(SSDP_OTHER, r));
  CHECK(!parse_ssdp_reply("NOTIFY * HTTP/1.1\r\nST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n\r\n", r));
}

static void test_description_play5_has_line_in() {
  DeviceInfo d;
  CHECK(parse_device_description(device_xml("RINCON_AAAAAAAAAAAA01400", "Living Room", "Sonos Play:5", true), d));
  CHECK_EQ(d.uuid, "RINCON_AAAAAAAAAAAA01400");
  CHECK_EQ(d.room, "Living Room");
  CHECK_EQ(d.model, "Sonos Play:5");
  CHECK(d.has_line_in);
}

static void test_description_beam_no_line_in() {
  DeviceInfo d;
  CHECK(parse_device_description(device_xml("RINCON_BBBBBBBBBBBB01400", "TV Room", "Sonos Beam", false), d));
  CHECK(!d.has_line_in);
}

static void test_topology_stereo_pair_one_room() {
  // Two Play:5s paired in the Den: the right one is hidden.
  std::string pair = "ChannelMapSet=\"RINCON_L:LF,LF;RINCON_R:RF,RF\"";
  std::string xml = topology_soap(group(
      "RINCON_L", member("RINCON_L", "192.168.1.30", "Den", pair) +
                      member("RINCON_R", "192.168.1.31", "Den", pair + " Invisible=\"1\"")));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  CHECK_EQ(groups.size(), 1u);
  CHECK_EQ(groups[0].coordinator_uuid, "RINCON_L");
  CHECK_EQ(groups[0].members.size(), 2u);
  CHECK(groups[0].members[1].invisible);

  std::map<std::string, DeviceInfo> devices = {{"RINCON_L", dev("RINCON_L", "Den", true)},
                                               {"RINCON_R", dev("RINCON_R", "Den", true)}};
  auto c = line_in_candidates(groups, devices);
  CHECK_EQ(c.size(), 1u);
  if (!c.empty()) {
    CHECK_EQ(c[0].uuid, "RINCON_L");
    CHECK_EQ(c[0].room, "Den");
    CHECK_EQ(c[0].ip, "192.168.1.30");
  }
}

static void test_own_line_in_pair_partner_counts_other_room_doesnt() {
  // The Den pair is grouped with the Kitchen, which has a CD player on its
  // line-in. The turntable is on the Den pair.
  std::string pair = "ChannelMapSet=\"RINCON_L:LF,LF;RINCON_R:RF,RF\"";
  std::string xml = topology_soap(group(
      "RINCON_L", member("RINCON_L", "192.168.1.30", "Den", pair) +
                      member("RINCON_R", "192.168.1.31", "Den", pair + " Invisible=\"1\"") +
                      member("RINCON_K", "192.168.1.32", "Kitchen")));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  CHECK(is_own_line_in(groups, "RINCON_L", "RINCON_L"));
  CHECK(is_own_line_in(groups, "RINCON_L", "RINCON_R"));   // the other half of the pair
  CHECK(!is_own_line_in(groups, "RINCON_L", "RINCON_K"));  // another room's line-in
  CHECK(!is_own_line_in(groups, "RINCON_L", "RINCON_X"));  // not in the household
  CHECK(!is_own_line_in(groups, "RINCON_L", ""));
  CHECK(!is_own_line_in({}, "RINCON_L", "RINCON_R"));      // nothing known yet: only exact
}

static void test_own_line_in_surround_doesnt_count() {
  std::string xml = topology_soap(
      group("RINCON_ARC",
            member("RINCON_ARC", "192.168.1.40", "Living Room", "",
                   satellite("RINCON_E1", "192.168.1.41", "Living Room"))));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  CHECK(!is_own_line_in(groups, "RINCON_ARC", "RINCON_E1"));
}

static void test_description_reads_s1_or_s2() {
  DeviceInfo d;
  CHECK(parse_device_description(device_xml("RINCON_C", "Office", "Sonos Connect", true, 2), d));
  CHECK_EQ(d.generation, 2);
  CHECK(parse_device_description(device_xml("RINCON_C", "Office", "Sonos Connect", true, 1), d));
  CHECK_EQ(d.generation, 1);
  CHECK(parse_device_description(device_xml("RINCON_C", "Office", "Sonos Connect", true), d));
  CHECK_EQ(d.generation, 0);  // not stated
}

static DeviceInfo devg(const std::string &uuid, const std::string &room, bool line_in, int generation) {
  DeviceInfo d = dev(uuid, room, line_in);
  d.generation = generation;
  return d;
}

static void test_found_summary_one_system() {
  // Wells's house on 2026-11-19: a Connect, a Beam with two Play:1
  // surrounds, a One and a Roam 2, all S2.
  std::string xml = topology_soap(
      group("RINCON_C", member("RINCON_C", "192.168.1.54", "Office")) +
      group("RINCON_B", member("RINCON_B", "192.168.1.93", "Living Room", "",
                               satellite("RINCON_P1", "192.168.1.19", "Living Room") +
                                   satellite("RINCON_P2", "192.168.1.20", "Living Room"))) +
      group("RINCON_O", member("RINCON_O", "192.168.1.48", "Family Room")) +
      group("RINCON_R", member("RINCON_R", "192.168.1.5", "Roam 2")));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  std::map<std::string, DeviceInfo> devices = {{"RINCON_C", devg("RINCON_C", "Office", true, 2)},
                                               {"RINCON_B", devg("RINCON_B", "Living Room", false, 2)},
                                               {"RINCON_O", devg("RINCON_O", "Family Room", false, 2)},
                                               {"RINCON_R", devg("RINCON_R", "Roam 2", false, 2)}};
  CHECK_EQ(found_summary(groups, devices, 255),
           "S2 system. With a line-in: Office. Without: Family Room, Living Room, Roam 2.");
}

static void test_found_summary_pair_is_one_room() {
  std::string pair = "ChannelMapSet=\"RINCON_L:LF,LF;RINCON_R:RF,RF\"";
  std::string xml = topology_soap(group(
      "RINCON_L", member("RINCON_L", "192.168.1.30", "Den", pair) +
                      member("RINCON_R", "192.168.1.31", "Den", pair + " Invisible=\"1\"")));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  std::map<std::string, DeviceInfo> devices = {{"RINCON_L", devg("RINCON_L", "Den", true, 1)},
                                               {"RINCON_R", devg("RINCON_R", "Den", true, 1)}};
  CHECK_EQ(found_summary(groups, devices, 255), "S1 system. With a line-in: Den.");
}

static void test_found_summary_two_systems_unknown_and_silent() {
  // Two households (the search tags each group with its household), one
  // without a stated generation, and one speaker whose description didn't
  // come back.
  std::vector<Group> groups;
  Group a;
  a.coordinator_uuid = "RINCON_K";
  a.household = 1;
  a.members.push_back(Member{"RINCON_K", "Kitchen", "192.168.1.40", false, false});
  a.members.push_back(Member{"RINCON_D", "Den", "192.168.1.41", false, false});
  Group b;
  b.coordinator_uuid = "RINCON_B";
  b.household = 0;
  b.members.push_back(Member{"RINCON_B", "Bedroom", "192.168.1.42", false, false});
  groups.push_back(a);
  groups.push_back(b);
  std::map<std::string, DeviceInfo> devices = {{"RINCON_K", devg("RINCON_K", "Kitchen", false, 1)},
                                               {"RINCON_B", devg("RINCON_B", "Bedroom", false, 0)}};
  // Systems in order of their first room: Bedroom's, then Den's.
  CHECK_EQ(found_summary(groups, devices, 255),
           "Sonos system. Without: Bedroom. S1 system. Without: Kitchen. Not answering: Den.");
}

static void test_found_summary_nothing() {
  CHECK_EQ(found_summary({}, {}, 255), "No Sonos speakers found.");
}

static void test_found_summary_stays_under_limit() {
  // Home Assistant drops a state longer than 255 characters.
  std::vector<Group> groups;
  std::map<std::string, DeviceInfo> devices;
  for (int i = 0; i < 30; i++) {
    std::string uuid = "RINCON_" + std::to_string(i);
    std::string room = "Room with a long name " + std::to_string(10 + i);
    Group g;
    g.coordinator_uuid = uuid;
    g.members.push_back(Member{uuid, room, "192.168.1." + std::to_string(i), false, false});
    groups.push_back(g);
    devices[uuid] = devg(uuid, room, i == 0, 2);
  }
  std::string s = found_summary(groups, devices, 255);
  CHECK(s.size() <= 255);
  CHECK(s.find("S2 system. With a line-in: Room with a long name 10. Without: ") == 0);
  CHECK(s.find(" more.") != std::string::npos);
}

static void test_topology_ht_with_era_surrounds_excludes_surrounds() {
  // Arc with two Era 100 surrounds: the Arc has no line-in, and the Eras lose
  // theirs while bonded as surrounds.
  std::string xml = topology_soap(
      group("RINCON_ARC",
            member("RINCON_ARC", "192.168.1.40", "Living Room",
                   "HTSatChanMapSet=\"RINCON_ARC:LF,RF;RINCON_E1:LR;RINCON_E2:RR\"",
                   satellite("RINCON_E1", "192.168.1.41", "Living Room") +
                       satellite("RINCON_E2", "192.168.1.42", "Living Room"))));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  CHECK_EQ(groups.size(), 1u);
  CHECK_EQ(groups[0].members.size(), 3u);
  int satellites = 0;
  for (auto &m : groups[0].members)
    satellites += m.ht_satellite ? 1 : 0;
  CHECK_EQ(satellites, 2);

  std::map<std::string, DeviceInfo> devices = {{"RINCON_ARC", dev("RINCON_ARC", "Living Room", false)},
                                               {"RINCON_E1", dev("RINCON_E1", "Living Room", true)},
                                               {"RINCON_E2", dev("RINCON_E2", "Living Room", true)}};
  CHECK(line_in_candidates(groups, devices).empty());
}

static void test_topology_amp_ht_primary_offered() {
  std::string xml = topology_soap(
      group("RINCON_AMP", member("RINCON_AMP", "192.168.1.50", "Family Room",
                                 "HTSatChanMapSet=\"RINCON_AMP:LF,RF;RINCON_SUB:SW\"",
                                 satellite("RINCON_SUB", "192.168.1.51", "Family Room"))));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  std::map<std::string, DeviceInfo> devices = {{"RINCON_AMP", dev("RINCON_AMP", "Family Room", true)},
                                               {"RINCON_SUB", dev("RINCON_SUB", "Family Room", false)}};
  auto c = line_in_candidates(groups, devices);
  CHECK_EQ(c.size(), 1u);
  if (!c.empty())
    CHECK_EQ(c[0].uuid, "RINCON_AMP");
}

static void test_candidates_sorted_and_skip_unknown_devices() {
  std::string xml = topology_soap(group("RINCON_K", member("RINCON_K", "192.168.1.60", "Kitchen")) +
                                  group("RINCON_B", member("RINCON_B", "192.168.1.61", "Bedroom")) +
                                  group("RINCON_X", member("RINCON_X", "192.168.1.62", "Garage")));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  CHECK_EQ(groups.size(), 3u);
  // Garage's description wasn't fetched, so it isn't offered.
  std::map<std::string, DeviceInfo> devices = {{"RINCON_K", dev("RINCON_K", "Kitchen", true)},
                                               {"RINCON_B", dev("RINCON_B", "Bedroom", true)}};
  auto c = line_in_candidates(groups, devices);
  CHECK_EQ(c.size(), 2u);
  if (c.size() == 2) {
    CHECK_EQ(c[0].room, "Bedroom");
    CHECK_EQ(c[1].room, "Kitchen");
  }
}

static void test_resolve_target_grouped_uses_coordinator() {
  // The Den (with the turntable) has been grouped into the Kitchen.
  std::string xml = topology_soap(group(
      "RINCON_K", member("RINCON_K", "192.168.1.60", "Kitchen") + member("RINCON_D", "192.168.1.61", "Den")));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  Target t;
  CHECK(resolve_target(groups, "RINCON_D", t));
  CHECK_EQ(t.control_ip, "192.168.1.60");
  CHECK_EQ(t.line_in_uuid, "RINCON_D");
  CHECK(!resolve_target(groups, "RINCON_NOPE", t));
}

static void test_choose_keeps_saved_when_present() {
  std::vector<Candidate> c = {{"RINCON_A", "A", "1.1.1.1"}, {"RINCON_B", "B", "1.1.1.2"}};
  std::string out;
  CHECK(choose_speaker(c, "RINCON_B", out) == Choice::KEEP_SAVED);
  CHECK_EQ(out, "RINCON_B");
}

static void test_choose_keeps_saved_when_absent_this_search() {
  std::vector<Candidate> c = {{"RINCON_A", "A", "1.1.1.1"}};
  std::string out;
  CHECK(choose_speaker(c, "RINCON_GONE", out) == Choice::KEEP_SAVED);
  CHECK_EQ(out, "RINCON_GONE");
}

static void test_choose_auto_picks_single() {
  std::vector<Candidate> c = {{"RINCON_A", "A", "1.1.1.1"}};
  std::string out;
  CHECK(choose_speaker(c, "", out) == Choice::AUTO_PICKED);
  CHECK_EQ(out, "RINCON_A");
}

static void test_choose_two_needs_user() {
  std::vector<Candidate> c = {{"RINCON_A", "A", "1.1.1.1"}, {"RINCON_B", "B", "1.1.1.2"}};
  std::string out;
  CHECK(choose_speaker(c, "", out) == Choice::NEEDS_USER);
  CHECK(out.empty());
}

static void test_choose_none_found() {
  std::string out;
  CHECK(choose_speaker({}, "", out) == Choice::NONE_FOUND);
}

static void test_transport_state_playing() {
  std::string s;
  CHECK(parse_transport_state(transport_soap("PLAYING"), s));
  CHECK_EQ(s, "PLAYING");
  CHECK(parse_transport_state(transport_soap("PAUSED_PLAYBACK"), s));
  CHECK_EQ(s, "PAUSED_PLAYBACK");
  CHECK(!parse_transport_state("<s:Envelope><s:Body><s:Fault/></s:Body></s:Envelope>", s));
}

static void test_line_in_uri_prefix() {
  std::string uri;
  CHECK(parse_current_uri(media_soap("x-rincon-stream:RINCON_D"), uri));
  CHECK(is_line_in_uri(uri));
  CHECK_EQ(line_in_source(uri), "RINCON_D");
  CHECK(parse_current_uri(media_soap("x-sonos-spotify:spotify%3atrack%3a1?sid=9&amp;flags=8224"), uri));
  CHECK_EQ(uri, "x-sonos-spotify:spotify%3atrack%3a1?sid=9&flags=8224");
  CHECK(!is_line_in_uri(uri));
  CHECK_EQ(line_in_source(uri), "");
  // An empty queue reports an empty CurrentURI, which still parses.
  CHECK(parse_current_uri(media_soap(""), uri));
  CHECK_EQ(uri, "");
}

static void test_room_name_with_quotes_and_utf8_survives() {
  // ZoneName="Ma &quot;Den&quot; &amp; Café" inside the topology.
  std::string room_attr = "Ma &quot;Den&quot; &amp; Caf\xC3\xA9";
  std::string xml = topology_soap(group("RINCON_Q", member("RINCON_Q", "192.168.1.70", room_attr)));
  std::vector<Group> groups;
  CHECK(parse_zone_group_topology(xml, groups));
  CHECK_EQ(groups.size(), 1u);
  if (!groups.empty() && !groups[0].members.empty())
    CHECK_EQ(groups[0].members[0].room, "Ma \"Den\" & Caf\xC3\xA9");

  DeviceInfo d;
  CHECK(parse_device_description(device_xml("RINCON_Q", "Kid&apos;s Room &lt;2&gt;", "Sonos Five", true), d));
  CHECK_EQ(d.room, "Kid's Room <2>");
}

static void test_truncated_xml_returns_false() {
  std::string full = topology_soap(group("RINCON_A", member("RINCON_A", "192.168.1.80", "Office")));
  std::vector<Group> groups;
  CHECK(!parse_zone_group_topology(full.substr(0, full.size() / 2), groups));
  CHECK(!parse_zone_group_topology("", groups));

  std::string desc = device_xml("RINCON_A", "Office", "Sonos Five", true);
  DeviceInfo d;
  CHECK(!parse_device_description(desc.substr(0, 120), d));

  std::string s;
  CHECK(!parse_transport_state("<CurrentTransportState>PLAY", s));
}

static void test_xml_unescape() {
  CHECK_EQ(xml_unescape("a &amp; b &lt;c&gt; &quot;d&quot; &apos;e&apos;"), "a & b <c> \"d\" 'e'");
  CHECK_EQ(xml_unescape("&#65;&#x42;"), "AB");
  CHECK_EQ(xml_unescape("&#233;"), "\xC3\xA9");
  CHECK_EQ(xml_unescape("broken &amp no semicolon"), "broken &amp no semicolon");
  CHECK_EQ(xml_unescape("&unknown;"), "&unknown;");
}

static void test_host_from_url() {
  CHECK_EQ(host_from_url("http://10.0.0.5:1400/xml/device_description.xml"), "10.0.0.5");
  CHECK_EQ(host_from_url("http://10.0.0.5/x"), "10.0.0.5");
  CHECK_EQ(host_from_url("garbage"), "");
}

int main() {
  const std::vector<std::pair<const char *, std::function<void()>>> tests = {
      {"ssdp_accepts_zoneplayer", test_ssdp_accepts_zoneplayer},
      {"ssdp_rejects_other_st", test_ssdp_rejects_other_st},
      {"description_play5_has_line_in", test_description_play5_has_line_in},
      {"description_beam_no_line_in", test_description_beam_no_line_in},
      {"description_reads_s1_or_s2", test_description_reads_s1_or_s2},
      {"found_summary_one_system", test_found_summary_one_system},
      {"found_summary_pair_is_one_room", test_found_summary_pair_is_one_room},
      {"found_summary_two_systems_unknown_and_silent", test_found_summary_two_systems_unknown_and_silent},
      {"found_summary_nothing", test_found_summary_nothing},
      {"found_summary_stays_under_limit", test_found_summary_stays_under_limit},
      {"topology_stereo_pair_one_room", test_topology_stereo_pair_one_room},
      {"own_line_in_pair_partner_counts_other_room_doesnt", test_own_line_in_pair_partner_counts_other_room_doesnt},
      {"own_line_in_surround_doesnt_count", test_own_line_in_surround_doesnt_count},
      {"topology_ht_with_era_surrounds_excludes_surrounds", test_topology_ht_with_era_surrounds_excludes_surrounds},
      {"topology_amp_ht_primary_offered", test_topology_amp_ht_primary_offered},
      {"candidates_sorted_and_skip_unknown_devices", test_candidates_sorted_and_skip_unknown_devices},
      {"resolve_target_grouped_uses_coordinator", test_resolve_target_grouped_uses_coordinator},
      {"choose_keeps_saved_when_present", test_choose_keeps_saved_when_present},
      {"choose_keeps_saved_when_absent_this_search", test_choose_keeps_saved_when_absent_this_search},
      {"choose_auto_picks_single", test_choose_auto_picks_single},
      {"choose_two_needs_user", test_choose_two_needs_user},
      {"choose_none_found", test_choose_none_found},
      {"transport_state_playing", test_transport_state_playing},
      {"line_in_uri_prefix", test_line_in_uri_prefix},
      {"room_name_with_quotes_and_utf8_survives", test_room_name_with_quotes_and_utf8_survives},
      {"truncated_xml_returns_false", test_truncated_xml_returns_false},
      {"xml_unescape", test_xml_unescape},
      {"host_from_url", test_host_from_url},
  };
  for (auto &t : tests) {
    int before = g_failures;
    t.second();
    std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.first);
  }
  if (g_failures) {
    std::printf("%d of %d checks failed\n", g_failures, g_checks);
    return 1;
  }
  std::printf("all %zu tests passed (%d checks)\n", tests.size(), g_checks);
  return 0;
}
