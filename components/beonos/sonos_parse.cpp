// SPDX-License-Identifier: GPL-3.0-or-later
#include "sonos_parse.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace beonos {
namespace sonos {

const char *const ZONE_PLAYER_ST = "urn:schemas-upnp-org:device:ZonePlayer:1";

namespace {

std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

bool iequals(const std::string &a, const char *b) {
  size_t n = 0;
  for (; b[n] != '\0'; ++n) {
    if (n >= a.size() || std::tolower(static_cast<unsigned char>(a[n])) != std::tolower(static_cast<unsigned char>(b[n])))
      return false;
  }
  return n == a.size();
}

// Text between <tag> and </tag>, first occurrence at or after `from`.
bool element_text(const std::string &xml, const std::string &tag, std::string &out, size_t from = 0) {
  const std::string open = "<" + tag + ">";
  const std::string close = "</" + tag + ">";
  size_t s = xml.find(open, from);
  if (s == std::string::npos)
    return false;
  s += open.size();
  size_t e = xml.find(close, s);
  if (e == std::string::npos)
    return false;
  out = xml.substr(s, e - s);
  return true;
}

// Value of name="..." inside one start tag. Values are returned unescaped.
bool tag_attr(const std::string &tag, const char *name, std::string &out) {
  const std::string key = std::string(" ") + name + "=\"";
  size_t s = tag.find(key);
  if (s == std::string::npos)
    return false;
  s += key.size();
  size_t e = tag.find('"', s);
  if (e == std::string::npos)
    return false;
  out = xml_unescape(tag.substr(s, e - s));
  return true;
}

void append_utf8(std::string &out, unsigned long cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x110000) {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

Member parse_member_tag(const std::string &tag, bool satellite) {
  Member m;
  tag_attr(tag, "UUID", m.uuid);
  tag_attr(tag, "ZoneName", m.room);
  std::string location, invisible;
  if (tag_attr(tag, "Location", location))
    m.ip = host_from_url(location);
  m.invisible = tag_attr(tag, "Invisible", invisible) && invisible == "1";
  m.ht_satellite = satellite;
  return m;
}

}  // namespace

std::string xml_unescape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '&') {
      out += s[i++];
      continue;
    }
    size_t semi = s.find(';', i + 1);
    // Entities are short; anything longer is a bare ampersand.
    if (semi == std::string::npos || semi - i > 10) {
      out += s[i++];
      continue;
    }
    std::string name = s.substr(i + 1, semi - i - 1);
    bool done = true;
    if (name == "amp")
      out += '&';
    else if (name == "lt")
      out += '<';
    else if (name == "gt")
      out += '>';
    else if (name == "quot")
      out += '"';
    else if (name == "apos")
      out += '\'';
    else if (name.size() > 1 && name[0] == '#') {
      char *end = nullptr;
      unsigned long cp = (name[1] == 'x' || name[1] == 'X') ? std::strtoul(name.c_str() + 2, &end, 16)
                                                            : std::strtoul(name.c_str() + 1, &end, 10);
      if (end != nullptr && *end == '\0' && cp > 0)
        append_utf8(out, cp);
      else
        done = false;
    } else {
      done = false;
    }
    if (done) {
      i = semi + 1;
    } else {
      out += s[i++];
    }
  }
  return out;
}

std::string host_from_url(const std::string &url) {
  const std::string scheme = "://";
  size_t s = url.find(scheme);
  if (s == std::string::npos)
    return "";
  s += scheme.size();
  size_t e = url.find_first_of(":/", s);
  if (e == std::string::npos)
    e = url.size();
  return url.substr(s, e - s);
}

bool parse_ssdp_reply(const std::string &packet, SsdpReply &out) {
  if (packet.compare(0, 12, "HTTP/1.1 200") != 0)
    return false;
  std::string st, location, usn;
  size_t pos = packet.find("\r\n");
  while (pos != std::string::npos && pos + 2 < packet.size()) {
    size_t start = pos + 2;
    size_t end = packet.find("\r\n", start);
    std::string line = packet.substr(start, end == std::string::npos ? std::string::npos : end - start);
    pos = end;
    size_t colon = line.find(':');
    if (colon == std::string::npos)
      continue;
    std::string name = trim(line.substr(0, colon));
    std::string value = trim(line.substr(colon + 1));
    if (iequals(name, "ST"))
      st = value;
    else if (iequals(name, "LOCATION"))
      location = value;
    else if (iequals(name, "USN"))
      usn = value;
  }
  if (st != ZONE_PLAYER_ST || location.empty())
    return false;
  out.location = location;
  out.usn = usn;
  return true;
}

bool parse_device_description(const std::string &xml, DeviceInfo &out) {
  if (xml.find("</root>") == std::string::npos)
    return false;
  std::string udn, room, model;
  if (!element_text(xml, "UDN", udn) || !element_text(xml, "roomName", room))
    return false;
  element_text(xml, "modelName", model);
  udn = xml_unescape(udn);
  if (udn.compare(0, 5, "uuid:") == 0)
    udn = udn.substr(5);
  out.uuid = udn;
  out.room = xml_unescape(room);
  out.model = xml_unescape(model);
  out.has_line_in = xml.find("urn:schemas-upnp-org:service:AudioIn:1") != std::string::npos;
  return true;
}

bool parse_zone_group_topology(std::string soap, std::vector<Group> &out) {
  std::string escaped;
  if (!element_text(soap, "ZoneGroupState", escaped))
    return false;
  std::string().swap(soap);
  // The topology travels escaped inside the SOAP body. After one unescape its
  // attribute values are still entity-encoded, so '>' only ends tags.
  const std::string xml = xml_unescape(escaped);
  std::string().swap(escaped);
  if (xml.find("</ZoneGroups>") == std::string::npos)
    return false;

  std::vector<Group> groups;
  size_t pos = 0;
  while (true) {
    size_t g = xml.find("<ZoneGroup ", pos);
    if (g == std::string::npos)
      break;
    size_t g_tag_end = xml.find('>', g);
    size_t g_end = xml.find("</ZoneGroup>", g);
    if (g_tag_end == std::string::npos || g_end == std::string::npos)
      return false;
    Group group;
    tag_attr(xml.substr(g, g_tag_end - g), "Coordinator", group.coordinator_uuid);

    size_t m = g_tag_end;
    while (true) {
      size_t member = xml.find("<ZoneGroupMember ", m);
      size_t sat = xml.find("<Satellite ", m);
      size_t next = std::min(member, sat);
      if (next == std::string::npos || next > g_end)
        break;
      size_t tag_end = xml.find('>', next);
      if (tag_end == std::string::npos || tag_end > g_end)
        return false;
      group.members.push_back(parse_member_tag(xml.substr(next, tag_end - next), next == sat));
      m = tag_end;
    }
    groups.push_back(group);
    pos = g_end + 12;
  }
  out = groups;
  return true;
}

std::vector<Candidate> line_in_candidates(const std::vector<Group> &groups,
                                          const std::map<std::string, DeviceInfo> &devices) {
  std::vector<Candidate> out;
  for (const auto &group : groups) {
    for (const auto &m : group.members) {
      if (m.invisible || m.ht_satellite || m.uuid.empty())
        continue;
      auto it = devices.find(m.uuid);
      if (it == devices.end() || !it->second.has_line_in)
        continue;
      out.push_back(Candidate{m.uuid, m.room, m.ip});
    }
  }
  std::sort(out.begin(), out.end(), [](const Candidate &a, const Candidate &b) {
    return a.room != b.room ? a.room < b.room : a.uuid < b.uuid;
  });
  return out;
}

bool is_own_line_in(const std::vector<Group> &groups, const std::string &chosen_uuid,
                    const std::string &source_uuid) {
  if (source_uuid.empty() || chosen_uuid.empty())
    return false;
  if (source_uuid == chosen_uuid)
    return true;
  for (const auto &group : groups) {
    const Member *chosen = nullptr;
    const Member *source = nullptr;
    for (const auto &m : group.members) {
      if (m.uuid == chosen_uuid)
        chosen = &m;
      if (m.uuid == source_uuid)
        source = &m;
    }
    // The two halves of a stereo pair are one room in one group.
    if (chosen != nullptr && source != nullptr && !source->ht_satellite && source->room == chosen->room)
      return true;
  }
  return false;
}

bool resolve_target(const std::vector<Group> &groups, const std::string &chosen_uuid, Target &out) {
  for (const auto &group : groups) {
    bool contains = false;
    for (const auto &m : group.members)
      contains |= m.uuid == chosen_uuid;
    if (!contains)
      continue;
    for (const auto &m : group.members) {
      if (m.uuid == group.coordinator_uuid && !m.ip.empty()) {
        out.control_ip = m.ip;
        out.line_in_uuid = chosen_uuid;
        return true;
      }
    }
  }
  return false;
}

Choice choose_speaker(const std::vector<Candidate> &candidates, const std::string &saved_uuid,
                      std::string &out_uuid) {
  out_uuid.clear();
  if (!saved_uuid.empty()) {
    out_uuid = saved_uuid;
    return Choice::KEEP_SAVED;
  }
  if (candidates.size() == 1) {
    out_uuid = candidates[0].uuid;
    return Choice::AUTO_PICKED;
  }
  return candidates.empty() ? Choice::NONE_FOUND : Choice::NEEDS_USER;
}

bool parse_transport_state(const std::string &soap, std::string &state) {
  std::string s;
  if (!element_text(soap, "CurrentTransportState", s) || s.empty())
    return false;
  state = xml_unescape(s);
  return true;
}

bool parse_current_uri(const std::string &soap, std::string &uri) {
  std::string s;
  if (element_text(soap, "CurrentURI", s)) {
    uri = xml_unescape(s);
    return true;
  }
  // An empty element can come back self-closed.
  if (soap.find("<CurrentURI/>") != std::string::npos || soap.find("<CurrentURI />") != std::string::npos) {
    uri.clear();
    return true;
  }
  return false;
}

bool is_line_in_uri(const std::string &uri) { return uri.compare(0, 16, "x-rincon-stream:") == 0; }

std::string line_in_source(const std::string &uri) { return is_line_in_uri(uri) ? uri.substr(16) : ""; }

}  // namespace sonos
}  // namespace beonos
