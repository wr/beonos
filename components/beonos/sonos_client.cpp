// SPDX-License-Identifier: GPL-3.0-or-later
#include "sonos_client.h"

#ifdef USE_ESP32

#include <algorithm>
#include <cstring>

#include <esp_http_client.h>
#include <esp_timer.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include "esphome/core/log.h"

namespace beonos {

static const char *const TAG = "beonos.sonos";

static constexpr uint32_t POLL_INTERVAL_MS = 1000;
static constexpr uint32_t MAX_BACKOFF_MS = 60000;
static constexpr int HTTP_TIMEOUT_MS = 2000;
static constexpr size_t MAX_DESCRIPTION_BYTES = 32 * 1024;
static constexpr size_t MAX_TOPOLOGY_BYTES = 48 * 1024;
static constexpr size_t MAX_STATUS_BYTES = 8 * 1024;
static constexpr size_t MAX_SPEAKERS = 32;
// A home can have more than one Sonos system (an S1 and an S2 one, say).
static constexpr size_t MAX_HOUSEHOLDS = 3;
static constexpr uint32_t TASK_STACK_BYTES = 8192;

static const char *const AV_TRANSPORT_PATH = "/MediaRenderer/AVTransport/Control";
static const char *const AV_TRANSPORT = "urn:schemas-upnp-org:service:AVTransport:1";
static const char *const TOPOLOGY_PATH = "/ZoneGroupTopology/Control";
static const char *const TOPOLOGY = "urn:schemas-upnp-org:service:ZoneGroupTopology:1";

static uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

bool SonosClient::start() {
  BaseType_t ok = xTaskCreate(&SonosClient::task_entry_, "beonos_sonos", TASK_STACK_BYTES, this,
                              tskIDLE_PRIORITY + 1, &this->task_);
  return ok == pdPASS;
}

void SonosClient::set_target(const std::string &control_ip, const std::string &line_in_uuid) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (control_ip != this->control_ip_) {
    this->target_changed_ = true;
    this->poll_now_ = true;
  }
  this->control_ip_ = control_ip;
  this->line_in_uuid_ = line_in_uuid;
}

void SonosClient::request_search(const std::string &manual_ip) {
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->search_requested_ = true;
    this->search_manual_ip_ = manual_ip;
  }
  if (this->task_ != nullptr)
    xTaskNotifyGive(this->task_);
}

void SonosClient::send(Command command) {
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->commands_.push(command, now_ms());
  }
  if (this->task_ != nullptr)
    xTaskNotifyGive(this->task_);
}

bool SonosClient::take_search_result(SearchResult &out) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->has_search_result_)
    return false;
  out = std::move(this->search_result_);
  this->search_result_ = SearchResult();
  this->has_search_result_ = false;
  return true;
}

bool SonosClient::take_poll_result(PollResult &out) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (!this->has_poll_result_)
    return false;
  out = this->poll_result_;
  this->has_poll_result_ = false;
  return true;
}

void SonosClient::task_entry_(void *arg) { static_cast<SonosClient *>(arg)->run_(); }

void SonosClient::run_() {
  while (true) {
    // Sleep until there's work or the next poll is due.
    uint32_t wait_ms = 200;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    if (!this->network_up_) {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->commands_.drop_expired(now_ms());
      continue;
    }

    bool has_command = false, search = false, poll_now = false;
    Command command = Command::PLAY;
    std::string ip, line_in_uuid, manual_ip;
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      ip = this->control_ip_;
      line_in_uuid = this->line_in_uuid_;
      if (ip.empty())
        this->commands_.drop_expired(now_ms());
      if (!ip.empty() && this->commands_.pop(now_ms(), command)) {
        has_command = true;
      } else if (this->search_requested_) {
        search = true;
        manual_ip = this->search_manual_ip_;
        this->search_requested_ = false;
      }
      poll_now = this->poll_now_;
      this->poll_now_ = false;
      if (this->target_changed_) {
        this->target_changed_ = false;
        this->backoff_ms_ = 0;
        this->failures_ = 0;
        this->next_poll_ms_ = now_ms();
      }
    }

    if (has_command) {
      this->do_command_(command, ip, line_in_uuid);
      // Read the new state back straight away. (Due now, not at 0: the
      // signed comparison below would read 0 as the future after 24.9 days.)
      this->next_poll_ms_ = now_ms();
      continue;
    }
    if (search) {
      this->do_search_(manual_ip);
      continue;
    }
    if (ip.empty())
      continue;
    uint32_t now = now_ms();
    if (poll_now || static_cast<int32_t>(now - this->next_poll_ms_) >= 0) {
      this->do_poll_(ip);
    }
  }
}

void SonosClient::record_poll_failure_() {
  this->failures_++;
  this->backoff_ms_ = this->backoff_ms_ == 0 ? 2000 : std::min(this->backoff_ms_ * 2, MAX_BACKOFF_MS);
  this->next_poll_ms_ = now_ms() + this->backoff_ms_;
}

void SonosClient::do_poll_(const std::string &ip) {
  PollResult result;
  std::string response;
  bool ok = this->soap_(ip, AV_TRANSPORT_PATH, AV_TRANSPORT, "GetTransportInfo", "<InstanceID>0</InstanceID>",
                        response, MAX_STATUS_BYTES) &&
            sonos::parse_transport_state(response, result.transport_state);
  if (ok) {
    ok = this->soap_(ip, AV_TRANSPORT_PATH, AV_TRANSPORT, "GetMediaInfo", "<InstanceID>0</InstanceID>", response,
                     MAX_STATUS_BYTES) &&
         sonos::parse_current_uri(response, result.current_uri);
  }
  result.ok = ok;
  if (ok) {
    if (this->failures_ > 0)
      ESP_LOGI(TAG, "Speaker at %s answering again", ip.c_str());
    this->failures_ = 0;
    this->backoff_ms_ = 0;
    this->next_poll_ms_ = now_ms() + POLL_INTERVAL_MS;
  } else {
    if (this->failures_ == 0)
      ESP_LOGW(TAG, "Speaker at %s isn't answering; backing off", ip.c_str());
    this->record_poll_failure_();
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->poll_result_ = result;
  this->has_poll_result_ = true;
}

void SonosClient::do_command_(Command command, const std::string &ip, const std::string &line_in_uuid) {
  std::string response;
  bool ok = false;
  switch (command) {
    case Command::PLAY:
      ESP_LOGI(TAG, "Play");
      ok = this->soap_(ip, AV_TRANSPORT_PATH, AV_TRANSPORT, "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>",
                       response, MAX_STATUS_BYTES);
      break;
    case Command::PAUSE:
      ESP_LOGI(TAG, "Pause");
      ok = this->soap_(ip, AV_TRANSPORT_PATH, AV_TRANSPORT, "Pause", "<InstanceID>0</InstanceID>", response,
                       MAX_STATUS_BYTES);
      break;
    case Command::SWITCH_TO_LINE_IN:
      if (line_in_uuid.empty()) {
        ESP_LOGW(TAG, "No line-in known; can't switch");
        return;
      }
      ESP_LOGI(TAG, "Switch to line-in of %s", line_in_uuid.c_str());
      ok = this->soap_(ip, AV_TRANSPORT_PATH, AV_TRANSPORT, "SetAVTransportURI",
                       "<InstanceID>0</InstanceID><CurrentURI>x-rincon-stream:" + line_in_uuid +
                           "</CurrentURI><CurrentURIMetaData></CurrentURIMetaData>",
                       response, MAX_STATUS_BYTES);
      break;
  }
  if (!ok)
    ESP_LOGW(TAG, "Command to %s failed", ip.c_str());
}

void SonosClient::do_search_(const std::string &manual_ip) {
  this->searching_ = true;
  SearchResult result;

  std::vector<std::string> ips;
  if (!manual_ip.empty()) {
    ESP_LOGI(TAG, "Checking the speaker at %s", manual_ip.c_str());
    ips.push_back(manual_ip);
  } else {
    ESP_LOGI(TAG, "Searching for Sonos speakers");
    for (const auto &location : this->ssdp_search_()) {
      std::string host = sonos::host_from_url(location);
      if (!host.empty() && std::find(ips.begin(), ips.end(), host) == ips.end())
        ips.push_back(host);
    }
    ESP_LOGI(TAG, "%u speaker(s) answered", static_cast<unsigned>(ips.size()));
  }

  // One speaker's topology describes its whole household. Read one from each
  // household: a speaker that isn't in any topology read so far belongs to
  // another one.
  std::vector<std::string> covered;
  size_t households = 0;
  for (const auto &ip : ips) {
    if (households >= MAX_HOUSEHOLDS)
      break;
    if (std::find(covered.begin(), covered.end(), ip) != covered.end())
      continue;
    std::vector<sonos::Group> groups;
    if (!this->fetch_topology_(ip, groups))
      continue;
    households++;
    covered.push_back(ip);
    for (auto &group : groups) {
      for (const auto &m : group.members)
        if (!m.ip.empty())
          covered.push_back(m.ip);
      bool known = std::any_of(result.groups.begin(), result.groups.end(), [&group](const sonos::Group &g) {
        return g.coordinator_uuid == group.coordinator_uuid;
      });
      if (!known)
        result.groups.push_back(std::move(group));
    }
  }
  const bool have_topology = households > 0;
  if (households > 1)
    ESP_LOGI(TAG, "Found %u Sonos systems", static_cast<unsigned>(households));

  if (have_topology) {
    size_t fetched = 0;
    for (const auto &group : result.groups) {
      for (const auto &m : group.members) {
        if (m.invisible || m.ht_satellite || m.ip.empty() || fetched >= MAX_SPEAKERS)
          continue;
        sonos::DeviceInfo info;
        if (this->fetch_description_(m.ip, info))
          result.devices[info.uuid] = info;
        fetched++;
      }
    }
  } else {
    // No topology: treat every speaker that answered as its own room.
    for (const auto &ip : ips) {
      sonos::DeviceInfo info;
      if (!this->fetch_description_(ip, info))
        continue;
      result.devices[info.uuid] = info;
      sonos::Group group;
      group.coordinator_uuid = info.uuid;
      sonos::Member m;
      m.uuid = info.uuid;
      m.room = info.room;
      m.ip = ip;
      group.members.push_back(m);
      result.groups.push_back(group);
    }
  }

  if (!manual_ip.empty()) {
    for (const auto &group : result.groups)
      for (const auto &m : group.members)
        if (m.ip == manual_ip && !m.ht_satellite)
          result.manual_uuid = m.uuid;
  }
  result.found_any = !result.devices.empty();

  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->search_result_ = std::move(result);
    this->has_search_result_ = true;
  }
  this->searching_ = false;
}

std::vector<std::string> SonosClient::ssdp_search_() {
  std::vector<std::string> locations;
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGW(TAG, "Couldn't open a socket for the speaker search");
    return locations;
  }
  int yes = 1;
  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
  uint8_t ttl = 2;
  setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  struct timeval tv = {0, 250 * 1000};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  static const char *const request =
      "M-SEARCH * HTTP/1.1\r\n"
      "HOST: 239.255.255.250:1900\r\n"
      "MAN: \"ssdp:discover\"\r\n"
      "MX: 1\r\n"
      "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
      "\r\n";
  struct sockaddr_in multicast = {};
  multicast.sin_family = AF_INET;
  multicast.sin_port = htons(1900);
  multicast.sin_addr.s_addr = inet_addr("239.255.255.250");
  struct sockaddr_in broadcast = multicast;
  broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);

  char buf[1024];
  const uint32_t start = now_ms();
  uint32_t sent = 0;
  while (now_ms() - start < 3500) {
    // Send three times, a second apart, in case a packet is lost.
    if (sent < 3 && now_ms() - start >= sent * 1000) {
      sendto(sock, request, strlen(request), 0, reinterpret_cast<struct sockaddr *>(&multicast), sizeof(multicast));
      sendto(sock, request, strlen(request), 0, reinterpret_cast<struct sockaddr *>(&broadcast), sizeof(broadcast));
      sent++;
    }
    int len = recv(sock, buf, sizeof(buf) - 1, 0);
    if (len <= 0)
      continue;
    buf[len] = '\0';
    sonos::SsdpReply reply;
    if (sonos::parse_ssdp_reply(std::string(buf, len), reply) &&
        std::find(locations.begin(), locations.end(), reply.location) == locations.end() &&
        locations.size() < MAX_SPEAKERS) {
      locations.push_back(reply.location);
    }
  }
  close(sock);
  return locations;
}

bool SonosClient::fetch_description_(const std::string &ip, sonos::DeviceInfo &out) {
  std::string body;
  if (!this->http_("http://" + ip + ":1400/xml/device_description.xml", nullptr, "", body, MAX_DESCRIPTION_BYTES))
    return false;
  return sonos::parse_device_description(body, out);
}

bool SonosClient::fetch_topology_(const std::string &ip, std::vector<sonos::Group> &out) {
  std::string response;
  if (!this->soap_(ip, TOPOLOGY_PATH, TOPOLOGY, "GetZoneGroupState", "", response, MAX_TOPOLOGY_BYTES))
    return false;
  return sonos::parse_zone_group_topology(std::move(response), out);
}

bool SonosClient::soap_(const std::string &ip, const char *path, const char *service, const char *action,
                        const std::string &args, std::string &response, size_t max_len) {
  std::string body = std::string("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                                 "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:") +
                     action + " xmlns:u=\"" + service + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>";
  std::string soap_action = std::string("\"") + service + "#" + action + "\"";
  return this->http_("http://" + ip + ":1400" + path, soap_action.c_str(), body, response, max_len);
}

bool SonosClient::http_(const std::string &url, const char *soap_action, const std::string &body,
                        std::string &response, size_t max_len) {
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.method = soap_action != nullptr ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  config.disable_auto_redirect = true;
  config.buffer_size = 1024;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr)
    return false;
  if (soap_action != nullptr) {
    esp_http_client_set_header(client, "Content-Type", "text/xml; charset=\"utf-8\"");
    esp_http_client_set_header(client, "SOAPACTION", soap_action);
  }

  bool ok = false;
  response.clear();
  if (esp_http_client_open(client, static_cast<int>(body.size())) == ESP_OK) {
    if (body.empty() || esp_http_client_write(client, body.data(), static_cast<int>(body.size())) ==
                            static_cast<int>(body.size())) {
      esp_http_client_fetch_headers(client);
      int status = esp_http_client_get_status_code(client);
      char chunk[512];
      while (true) {
        int n = esp_http_client_read(client, chunk, sizeof(chunk));
        if (n <= 0)
          break;
        if (response.size() + static_cast<size_t>(n) > max_len) {
          // Too big to be what we asked for; keep what fits.
          response.append(chunk, max_len - response.size());
          break;
        }
        response.append(chunk, n);
      }
      ok = status == 200;
    }
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

}  // namespace beonos

#endif  // USE_ESP32
