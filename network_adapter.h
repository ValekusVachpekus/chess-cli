/*
 * Copyright (C) 2026 Ilia Shchetkov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free-Software Foundation, either version 3 of the License, or
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

#ifndef NETWORK_ADAPTER_H
#define NETWORK_ADAPTER_H

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

class NetworkAdapter {
private:
  int server_fd;
  int client_fd;
  bool is_server;
  bool is_connected;
  std::string receive_buffer;

  // Переменные для фонового вещания
  std::atomic<bool> stop_broadcast{false};
  std::thread broadcast_thread;

  void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }

  // Фоновый поток, рассылающий UDP маяки
  void startBroadcast(int port) {
    stop_broadcast = false;
    broadcast_thread = std::thread([this, port]() {
      int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
      if (udp_fd < 0)
        return;
      int broadcastEnable = 1;
      setsockopt(udp_fd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable,
                 sizeof(broadcastEnable));

      std::string msg = "CHESS_SERVER_V1:" + std::to_string(port);
      while (!stop_broadcast) {
        // 1. Вещание для реальной локальной сети / Хотспота
        sockaddr_in b_addr{};
        b_addr.sin_family = AF_INET;
        b_addr.sin_port = htons(14889);
        b_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
        sendto(udp_fd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&b_addr,
               sizeof(b_addr));

        // 2. Вещание для локальных тестов на одном компьютере (localhost)
        sockaddr_in l_addr{};
        l_addr.sin_family = AF_INET;
        l_addr.sin_port = htons(14889);
        l_addr.sin_addr.s_addr = inet_addr("127.255.255.255");
        sendto(udp_fd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&l_addr,
               sizeof(l_addr));

        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      close(udp_fd);
    });
  }

public:
  NetworkAdapter()
      : server_fd(-1), client_fd(-1), is_server(false), is_connected(false) {}

  ~NetworkAdapter() { disconnect(); }

  // Статическая функция для сканирования локальной сети клиентом
  static std::map<std::string, int>
  discoverLocalServers(int timeout_ms = 2000) {
    std::map<std::string, int> servers;
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0)
      return servers;

    int reuse = 1;
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    // Allow several clients on the same host to each receive the beacons.
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
    sockaddr_in recv_addr{};
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(14889);
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    bind(udp_fd, (struct sockaddr *)&recv_addr, sizeof(recv_addr));

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    auto start = std::chrono::steady_clock::now();
    char buffer[128];
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < timeout_ms) {
      sockaddr_in sender_addr{};
      socklen_t addr_len = sizeof(sender_addr);
      ssize_t len = recvfrom(udp_fd, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr *)&sender_addr, &addr_len);
      if (len > 0) {
        buffer[len] = '\0';
        std::string msg(buffer);
        if (msg.find("CHESS_SERVER_V1:") == 0) {
          std::string ip = inet_ntoa(sender_addr.sin_addr);
          int p = std::stoi(msg.substr(16));
          servers[ip] = p; // Добавляем найденный сервер в список
        }
      }
    }
    close(udp_fd);
    return servers;
  }

  // Запуск сервера без блокировки (теперь работает прямо в TUI)
  bool startServer(int port) {
    is_server = true;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
      return false;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
      return false;
    if (listen(server_fd, 1) < 0)
      return false;

    setNonBlocking(server_fd); // Сервер больше не вешает приложение!
    startBroadcast(port);      // Начинаем кричать "Я ЗДЕСЬ!" в локальную сеть
    return true;
  }

  // Новая функция для опроса подключений в цикле TUI
  bool acceptClient() {
    if (is_connected)
      return true;
    sockaddr_in address{};
    socklen_t addrlen = sizeof(address);
    client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (client_fd >= 0) {
      setNonBlocking(client_fd);
      is_connected = true;
      stop_broadcast = true; // Кто-то подключился, выключаем UDP-маяк
      return true;
    }
    return false;
  }

  bool connectToServer(const std::string &ip, int port,
                       int timeout_ms = 5000) {
    is_server = false;
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0)
      return false;
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
      close(client_fd);
      client_fd = -1;
      return false;
    }

    // Non-blocking connect with a timeout so a dead/unreachable host does not
    // freeze the UI.
    setNonBlocking(client_fd);
    int res =
        connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (res < 0 && errno != EINPROGRESS) {
      close(client_fd);
      client_fd = -1;
      return false;
    }
    if (res < 0) {
      fd_set wset;
      FD_ZERO(&wset);
      FD_SET(client_fd, &wset);
      struct timeval tv;
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
      int sel = select(client_fd + 1, nullptr, &wset, nullptr, &tv);
      if (sel <= 0) { // timeout or error
        close(client_fd);
        client_fd = -1;
        return false;
      }
      int so_error = 0;
      socklen_t len = sizeof(so_error);
      if (getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 ||
          so_error != 0) {
        close(client_fd);
        client_fd = -1;
        return false;
      }
    }
    is_connected = true;
    return true;
  }

  bool sendMove(const std::string &move) {
    if (!is_connected)
      return false;
    std::string packet = move + "\n";
    size_t total = 0;
    auto start = std::chrono::steady_clock::now();
    while (total < packet.size()) {
      ssize_t n = send(client_fd, packet.c_str() + total, packet.size() - total,
                       MSG_NOSIGNAL);
      if (n > 0) {
        total += static_cast<size_t>(n);
        continue;
      }
      if (n < 0 &&
          (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        // Socket buffer momentarily full: retry briefly so the move is not lost.
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
        if (elapsed > 1000) {
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      is_connected = false;
      return false;
    }
    return true;
  }

  bool tryReadMove(std::string &move) {
    if (!is_connected)
      return false;
    char buffer[128];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
    if (bytes_read > 0) {
      receive_buffer.append(buffer, static_cast<size_t>(bytes_read));
    } else if (bytes_read == 0) {
      is_connected = false;
      move = "DISCONNECT";
      return true;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      is_connected = false;
      move = "DISCONNECT";
      return true;
    }
    size_t pos = receive_buffer.find('\n');
    if (pos != std::string::npos) {
      move = receive_buffer.substr(0, pos);
      receive_buffer.erase(0, pos + 1);
      return true;
    }
    return false;
  }

  bool isConnected() const { return is_connected; }

  void disconnect() {
    stop_broadcast = true;
    if (broadcast_thread.joinable())
      broadcast_thread.join();
    if (client_fd != -1) {
      close(client_fd);
      client_fd = -1;
    }
    if (server_fd != -1) {
      close(server_fd);
      server_fd = -1;
    }
    is_connected = false;
  }
};

#endif
