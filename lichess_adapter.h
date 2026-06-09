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

// Lichess Board API client: play live games against humans over HTTPS.
//   GET  /api/account                      -> my account id (for color)
//   POST /api/board/seek                   -> quick pairing (blocks until paired)
//   POST /api/challenge/{user}             -> challenge a player
//   POST /api/challenge/{id}/accept        -> accept an incoming challenge
//   GET  /api/stream/event                 -> NDJSON: challenge / gameStart
//   GET  /api/board/game/stream/{gameId}   -> NDJSON: gameFull / gameState
//   POST /api/board/game/{gameId}/move/{uci}
//   POST /api/board/game/{gameId}/{resign,abort}
// All requests carry "Authorization: Bearer <token>". Streams run on background
// threads; the engine state (move list, clocks, status) is the server's, guarded
// by a mutex and polled by the ncurses loop. Requires libcurl (-lcurl) and the
// vendored nlohmann/json (json.hpp). Included after gameboard.cpp so `Color`,
// `WHITE`/`BLACK` are visible.

#ifndef LICHESS_ADAPTER_H
#define LICHESS_ADAPTER_H

#include "json.hpp"
#include <atomic>
#include <cctype>
#include <curl/curl.h>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class LichessAdapter {
public:
  struct Challenge {
    std::string id;
    std::string label; // "challenger (rated, 5+0)" for the selection menu
  };

  explicit LichessAdapter(std::string token) : token_(std::move(token)) {}
  ~LichessAdapter() { disconnect(); }

  LichessAdapter(const LichessAdapter &) = delete;
  LichessAdapter &operator=(const LichessAdapter &) = delete;

  // Validates the token via GET /api/account. On success fills `accountId`
  // (lowercase) and returns ""; otherwise returns a human-readable error.
  std::string verifyToken(std::string &accountId) {
    long code = 0;
    std::string body = httpGet("https://lichess.org/api/account", code);
    if (code == 401)
      return "Invalid or expired token (HTTP 401)";
    if (code != 200 || body.empty())
      return "Cannot reach Lichess (HTTP " + std::to_string(code) + ")";
    try {
      auto j = nlohmann::json::parse(body);
      accountId_ = toLower(j.value("id", ""));
      accountId = accountId_;
      if (accountId_.empty())
        return "Account response missing id";
    } catch (const std::exception &e) {
      return std::string("Bad account JSON: ") + e.what();
    }
    return "";
  }

  // --- Lobby --------------------------------------------------------------

  void startEventStream() {
    if (!eventThread_.joinable())
      eventThread_ = std::thread([this]() { runStream(EVENT_STREAM); });
  }

  // Quick pairing. Keeps an HTTP request open until paired (run in background);
  // the actual game arrives as a `gameStart` event on the event stream.
  void seek(int minutes, int incSec, bool rated) {
    if (seekThread_.joinable())
      return;
    std::string fields = "time=" + std::to_string(minutes) +
                         "&increment=" + std::to_string(incSec) +
                         "&variant=standard&rated=" + (rated ? "true" : "false");
    seekThread_ = std::thread([this, fields]() {
      long code = 0;
      httpRequest("https://lichess.org/api/board/seek", "POST", fields, code,
                  /*longRunning=*/true);
    });
  }

  // Challenge a specific user. Returns "" on success, else an error string.
  std::string challengeUser(const std::string &user, int minutes, int incSec,
                            bool rated) {
    long code = 0;
    std::string fields =
        "clock.limit=" + std::to_string(minutes * 60) +
        "&clock.increment=" + std::to_string(incSec) +
        "&rated=" + (rated ? "true" : "false");
    std::string body = httpRequest(
        "https://lichess.org/api/challenge/" + user, "POST", fields, code,
        false);
    if (code == 200 || code == 201)
      return "";
    return "Challenge failed (HTTP " + std::to_string(code) + ")";
  }

  std::vector<Challenge> pollIncomingChallenges() {
    std::lock_guard<std::mutex> lk(mtx_);
    return challenges_;
  }

  bool acceptChallenge(const std::string &id) {
    long code = 0;
    httpRequest("https://lichess.org/api/challenge/" + id + "/accept", "POST",
                "", code, false);
    return code == 200;
  }

  // True once a game has started; outputs its id.
  bool gameStarted(std::string &id) {
    std::lock_guard<std::mutex> lk(mtx_);
    id = gameId_;
    return started_;
  }

  // --- In-game -----------------------------------------------------------

  void startGameStream(const std::string &id) {
    {
      std::lock_guard<std::mutex> lk(mtx_);
      gameId_ = id;
    }
    if (!gameThread_.joinable())
      gameThread_ = std::thread([this]() { runStream(GAME_STREAM); });
  }

  // True once the first `gameFull` arrived; outputs my color.
  bool ready(Color &myColor) {
    std::lock_guard<std::mutex> lk(mtx_);
    myColor = myColor_;
    return ready_;
  }

  std::vector<std::string> snapshotMoves() {
    std::lock_guard<std::mutex> lk(mtx_);
    return serverMoves_;
  }

  void clocks(long &wt, long &bt, long &wi, long &bi) {
    std::lock_guard<std::mutex> lk(mtx_);
    wt = wtime_;
    bt = btime_;
    wi = winc_;
    bi = binc_;
  }

  // True if the game finished; outputs the lichess status (mate/resign/...) and
  // winner ("white"/"black"/"").
  bool isGameOver(std::string &status, std::string &winner) {
    std::lock_guard<std::mutex> lk(mtx_);
    status = status_;
    winner = winner_;
    return gameOver_;
  }

  bool sendMove(const std::string &uci) {
    std::string id;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      id = gameId_;
    }
    if (id.empty())
      return false;
    long code = 0;
    httpRequest("https://lichess.org/api/board/game/" + id + "/move/" + uci,
                "POST", "", code, false);
    return code == 200;
  }

  bool resign() { return gameAction("resign"); }
  bool abort() { return gameAction("abort"); }

  void disconnect() {
    stop_ = true;
    if (eventThread_.joinable())
      eventThread_.join();
    if (gameThread_.joinable())
      gameThread_.join();
    if (seekThread_.joinable())
      seekThread_.join();
  }

private:
  enum StreamKind { EVENT_STREAM, GAME_STREAM };

  struct StreamCtx {
    LichessAdapter *self;
    StreamKind kind;
    std::string buf;
  };

  std::string token_;
  std::string accountId_;

  std::mutex mtx_;
  std::atomic<bool> stop_{false};
  std::thread eventThread_, gameThread_, seekThread_;

  // All fields below guarded by mtx_.
  std::string gameId_;
  bool started_ = false;
  bool ready_ = false;
  Color myColor_ = WHITE;
  std::vector<std::string> serverMoves_;
  long wtime_ = 0, btime_ = 0, winc_ = 0, binc_ = 0;
  bool gameOver_ = false;
  std::string status_, winner_;
  std::vector<Challenge> challenges_;

  bool gameAction(const std::string &action) {
    std::string id;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      id = gameId_;
    }
    if (id.empty())
      return false;
    long code = 0;
    httpRequest("https://lichess.org/api/board/game/" + id + "/" + action,
                "POST", "", code, false);
    return code == 200;
  }

  // --- HTTP helpers ------------------------------------------------------

  static size_t writeCallback(char *ptr, size_t size, size_t nmemb,
                              void *userdata) {
    std::string *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
  }

  struct curl_slist *authHeader() const {
    return curl_slist_append(nullptr,
                             ("Authorization: Bearer " + token_).c_str());
  }

  std::string httpGet(const std::string &url, long &code) {
    return httpRequest(url, "GET", "", code, false);
  }

  // One-shot request. `method` is "GET" or "POST"; `fields` is a urlencoded
  // body for POST. `longRunning` disables the timeout (for the seek request,
  // which blocks until paired) and aborts via the progress callback on stop_.
  std::string httpRequest(const std::string &url, const std::string &method,
                          const std::string &fields, long &code,
                          bool longRunning) {
    code = 0;
    CURL *curl = curl_easy_init();
    if (curl == nullptr)
      return "";
    std::string body;
    struct curl_slist *hdrs = authHeader();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "chess-tui/1.0");
    if (method == "POST") {
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fields.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                       static_cast<long>(fields.size()));
    }
    if (longRunning) {
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferAbort);
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    } else {
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    }
    if (curl_easy_perform(curl) == CURLE_OK)
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return body;
  }

  // --- Streaming ---------------------------------------------------------

  static int xferAbort(void *ud, curl_off_t, curl_off_t, curl_off_t,
                       curl_off_t) {
    return static_cast<LichessAdapter *>(ud)->stop_ ? 1 : 0;
  }

  static size_t streamWrite(char *ptr, size_t size, size_t nmemb,
                            void *userdata) {
    StreamCtx *c = static_cast<StreamCtx *>(userdata);
    if (c->self->stop_)
      return 0; // signal curl to abort the transfer
    c->buf.append(ptr, size * nmemb);
    size_t pos;
    while ((pos = c->buf.find('\n')) != std::string::npos) {
      std::string line = c->buf.substr(0, pos);
      c->buf.erase(0, pos + 1);
      if (!line.empty()) // empty lines are keepalive pings
        c->self->handleLine(c->kind, line);
    }
    return size * nmemb;
  }

  void runStream(StreamKind kind) {
    std::string url;
    if (kind == EVENT_STREAM) {
      url = "https://lichess.org/api/stream/event";
    } else {
      std::lock_guard<std::mutex> lk(mtx_);
      url = "https://lichess.org/api/board/game/stream/" + gameId_;
    }
    CURL *curl = curl_easy_init();
    if (curl == nullptr)
      return;
    StreamCtx ctx{this, kind, {}};
    struct curl_slist *hdrs = authHeader();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "chess-tui/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferAbort);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
  }

  void handleLine(StreamKind kind, const std::string &line) {
    nlohmann::json j;
    try {
      j = nlohmann::json::parse(line);
    } catch (const std::exception &) {
      return; // ignore malformed lines
    }
    std::string type = j.value("type", "");
    if (kind == EVENT_STREAM) {
      if (type == "gameStart" && j.contains("game")) {
        const auto &g = j["game"];
        std::string id = g.value("gameId", g.value("id", ""));
        if (!id.empty()) {
          std::lock_guard<std::mutex> lk(mtx_);
          gameId_ = id;
          started_ = true;
        }
      } else if (type == "challenge" && j.contains("challenge")) {
        const auto &c = j["challenge"];
        // Only inbound challenges addressed to us are actionable.
        if (c.value("direction", "in") != "in")
          return;
        Challenge ch;
        ch.id = c.value("id", "");
        std::string who;
        if (c.contains("challenger"))
          who = c["challenger"].value("name", c["challenger"].value("id", "?"));
        std::string speed = c.value("speed", "");
        bool rated = c.value("rated", false);
        ch.label = who + " (" + (rated ? "rated" : "casual") +
                   (speed.empty() ? "" : ", " + speed) + ")";
        if (ch.id.empty())
          return;
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto &existing : challenges_)
          if (existing.id == ch.id)
            return;
        challenges_.push_back(ch);
      }
      return;
    }

    // GAME_STREAM
    if (type == "gameFull") {
      Color color = WHITE;
      if (j.contains("white") && toLower(j["white"].value("id", "")) ==
                                     accountId_)
        color = WHITE;
      else if (j.contains("black") && toLower(j["black"].value("id", "")) ==
                                          accountId_)
        color = BLACK;
      std::lock_guard<std::mutex> lk(mtx_);
      myColor_ = color;
      ready_ = true;
      if (j.contains("state"))
        applyState(j["state"]);
    } else if (type == "gameState") {
      std::lock_guard<std::mutex> lk(mtx_);
      applyState(j);
    }
  }

  // Caller must hold mtx_. Updates the move list, clocks and finish status from
  // a Board API state object.
  void applyState(const nlohmann::json &s) {
    serverMoves_ = splitMoves(s.value("moves", ""));
    wtime_ = s.value("wtime", wtime_);
    btime_ = s.value("btime", btime_);
    winc_ = s.value("winc", winc_);
    binc_ = s.value("binc", binc_);
    std::string st = s.value("status", "");
    if (!st.empty()) {
      status_ = st;
      // Anything other than ongoing play means the game is over.
      if (st != "started" && st != "created") {
        gameOver_ = true;
        winner_ = s.value("winner", "");
      }
    }
  }

  static std::vector<std::string> splitMoves(const std::string &moves) {
    std::vector<std::string> out;
    std::istringstream iss(moves);
    std::string m;
    while (iss >> m)
      out.push_back(m);
    return out;
  }

  static std::string toLower(std::string s) {
    for (char &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }
};

#endif // LICHESS_ADAPTER_H
