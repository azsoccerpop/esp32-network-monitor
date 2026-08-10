#include "InfluxClient.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

namespace {

// Minimal percent-encoding, just enough for InfluxQL query strings (spaces
// and a handful of punctuation characters) -- not a general-purpose encoder.
String urlEncode(const String &s) {
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
      out += buf;
    }
  }
  return out;
}

}  // namespace

InfluxTestResult testInfluxConnection(const String &host, uint16_t port, const String &database,
                                       const String &username, const String &password) {
  InfluxTestResult result;

  if (host.length() == 0) {
    result.message = "Host is required.";
    return result;
  }

  const String baseUrl = "http://" + host + ":" + String(port);

  // Step 1: /ping -- confirms something InfluxDB-shaped is listening at
  // host:port at all, before attempting anything that needs auth.
  {
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    http.begin(baseUrl + "/ping");
    const int code = http.GET();

    if (code == 204 || code == 200) {
      result.reachable = true;
      result.influxVersion = http.header("X-Influxdb-Version");
    } else if (code < 0) {
      result.message = "Could not reach " + baseUrl + " (" + http.errorToString(code) + ").";
      http.end();
      return result;
    } else {
      result.message = "Unexpected response from " + baseUrl + "/ping (HTTP " + String(code) + "). "
                        "Is this actually an InfluxDB server?";
      http.end();
      return result;
    }
    http.end();
  }

  // Step 2: SHOW DATABASES -- confirms credentials (if any) are accepted,
  // and lets us check the named database actually exists. Run regardless of
  // whether a database was specified, since it's a useful auth check either
  // way.
  {
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    const String url = baseUrl + "/query?q=" + urlEncode("SHOW DATABASES");
    http.begin(url);
    if (username.length() > 0) {
      http.setAuthorization(username.c_str(), password.c_str());
    }
    const int code = http.GET();

    if (code == 401 || code == 403) {
      result.message = "Connected, but authentication failed -- check username/password.";
      http.end();
      return result;
    }
    if (code != 200) {
      result.message = "Connected, but SHOW DATABASES failed (HTTP " + String(code) + ").";
      http.end();
      return result;
    }

    const String body = http.getString();
    http.end();

    DynamicJsonDocument doc(2048);
    const DeserializationError err = deserializeJson(doc, body);
    if (err) {
      result.message = "Connected, but couldn't parse the database list response.";
      return result;
    }

    result.authOk = true;

    std::vector<String> databases;
    JsonArray series = doc["results"][0]["series"][0]["values"].as<JsonArray>();
    for (JsonArray row : series) {
      databases.push_back(row[0].as<String>());
    }

    if (database.length() == 0) {
      result.message = "Connected successfully" +
                        (result.influxVersion.length() ? (" (InfluxDB " + result.influxVersion + ")") : String("")) +
                        ". No database specified yet -- " + String(databases.size()) + " available.";
      return result;
    }

    for (const auto &db : databases) {
      if (db == database) {
        result.databaseFound = true;
        break;
      }
    }

    if (result.databaseFound) {
      result.message = "Connected successfully" +
                        (result.influxVersion.length() ? (" (InfluxDB " + result.influxVersion + ")") : String("")) +
                        ". Database '" + database + "' found.";
    } else {
      String available;
      for (size_t i = 0; i < databases.size(); ++i) {
        if (i > 0) available += ", ";
        available += databases[i];
      }
      result.message = "Connected and authenticated, but database '" + database + "' was not found. " +
                        "Available: " + (available.length() ? available : String("(none)"));
    }
  }

  return result;
}
