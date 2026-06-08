/*
 * Copyright (C) 2026 Ilia Shchetkov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Download a player's games from the chess.com public API (no auth required).
//   archives: https://api.chess.com/pub/player/{user}/games/archives
//   month:    <last archive url> -> {"games":[{pgn, white{}, black{}, ...}]}
// Requires libcurl (-lcurl). JSON parsed with the vendored nlohmann/json.

#ifndef CHESSCOM_ADAPTER_H
#define CHESSCOM_ADAPTER_H

#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <curl/curl.h>
#include <string>
#include <vector>

struct ChessComGame {
  std::string pgn;
  std::string whiteUser, blackUser;
  std::string whiteResult, blackResult;
  std::string endDate;   // formatted YYYY-MM-DD (UTC)
  std::string timeClass; // "blitz", "rapid", "daily", ...
};

class ChessComAdapter {
public:
  // Fills `out` with the latest month's standard-chess games (newest first).
  // Returns "" on success, otherwise a human-readable error for the status line.
  std::string fetchUserGames(const std::string &username,
                             std::vector<ChessComGame> &out) {
    out.clear();
    std::string user = toLower(trim(username));
    if (user.empty())
      return "Empty username";

    long code = 0;
    std::string archivesBody =
        httpGet("https://api.chess.com/pub/player/" + user + "/games/archives",
                code);
    if (code == 404)
      return "User '" + user + "' not found";
    if (code != 200 || archivesBody.empty())
      return "Failed to fetch archives (HTTP " + std::to_string(code) + ")";

    std::string lastArchive;
    try {
      auto j = nlohmann::json::parse(archivesBody);
      if (!j.contains("archives") || j["archives"].empty())
        return "No games found for '" + user + "'";
      lastArchive = j["archives"].back().get<std::string>();
    } catch (const std::exception &e) {
      return std::string("Bad archives JSON: ") + e.what();
    }

    std::string monthBody = httpGet(lastArchive, code);
    if (code != 200 || monthBody.empty())
      return "Failed to fetch games (HTTP " + std::to_string(code) + ")";

    try {
      auto j = nlohmann::json::parse(monthBody);
      if (!j.contains("games"))
        return "No games in latest month";
      for (const auto &g : j["games"]) {
        if (g.value("rules", "chess") != "chess")
          continue; // skip chess960 / variants this engine can't replay
        ChessComGame cg;
        cg.pgn = g.value("pgn", "");
        if (cg.pgn.empty())
          continue;
        if (g.contains("white")) {
          cg.whiteUser = g["white"].value("username", "");
          cg.whiteResult = g["white"].value("result", "");
        }
        if (g.contains("black")) {
          cg.blackUser = g["black"].value("username", "");
          cg.blackResult = g["black"].value("result", "");
        }
        cg.timeClass = g.value("time_class", "");
        cg.endDate = epochToDate(g.value("end_time", static_cast<long>(0)));
        out.push_back(cg);
      }
    } catch (const std::exception &e) {
      return std::string("Bad games JSON: ") + e.what();
    }

    if (out.empty())
      return "No standard games in latest month for '" + user + "'";
    std::reverse(out.begin(), out.end()); // newest first
    return "";
  }

private:
  static size_t writeCallback(char *ptr, size_t size, size_t nmemb,
                              void *userdata) {
    std::string *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
  }

  std::string httpGet(const std::string &url, long &httpCode) {
    httpCode = 0;
    CURL *curl = curl_easy_init();
    if (curl == nullptr)
      return "";
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "chess-tui/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK)
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    return body;
  }

  static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
      return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
  }

  static std::string toLower(std::string s) {
    for (char &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  static std::string epochToDate(long epoch) {
    if (epoch <= 0)
      return "";
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tmv;
    gmtime_r(&t, &tmv);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    return std::string(buf);
  }
};

#endif // CHESSCOM_ADAPTER_H
