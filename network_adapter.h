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
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class NetworkAdapter {
private:
  int server_fd;
  int client_fd;
  bool is_server;
  bool is_connected;

public:
  NetworkAdapter()
      : server_fd(-1), client_fd(-1), is_server(false), is_connected(false) {}

  ~NetworkAdapter() { disconnect(); }

  bool startServer(int port) {
    is_server = true;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
      std::cerr << "Error: Failed to create socket\n";
      return false;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
      std::cerr << "Error: Bind failed\n";
      return false;
    }

    if (listen(server_fd, 1) < 0) {
      std::cerr << "Error: Listen failed\n";
      return false;
    }

    std::cout << "[Network] Server started on port " << port
              << ". Waiting for opponent...\n";

    socklen_t addrlen = sizeof(address);
    client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (client_fd < 0) {
      std::cerr << "Error: Accept failed\n";
      return false;
    }

    setNonBlocking(client_fd);

    is_connected = true;
    std::cout << "[Network] Opponent connected!\n";
    return true;
  }

  bool connectToServer(const std::string &ip, int port) {
    is_server = false;

    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
      std::cerr << "Error: Failed to create socket\n";
      return false;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
      std::cerr << "Error: Invalid address / Address not supported\n";
      return false;
    }

    std::cout << "[Network] Connecting to " << ip << ":" << port << "...\n";
    if (connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) <
        0) {
      std::cerr << "Error: Connection failed\n";
      return false;
    }

    setNonBlocking(client_fd);

    is_connected = true;
    std::cout << "[Network] Successfully connected to server!\n";
    return true;
  }

  bool sendMove(const std::string &move) {
    if (!is_connected)
      return false;

    std::string packet = move + "\n";
    ssize_t bytes_sent = send(client_fd, packet.c_str(), packet.length(), 0);

    return bytes_sent > 0;
  }

  bool tryReadMove(std::string &move) {
    if (!is_connected)
      return false;

    char buffer[128];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read > 0) {
      buffer[bytes_read] = '\0';
      std::string data(buffer);

      // Ищем конец строки
      size_t pos = data.find('\n');
      if (pos != std::string::npos) {
        move = data.substr(0, pos);
        return true;
      }
    } else if (bytes_read == 0) {
      // Соперник отключился (EOF сокета)
      is_connected = false;
      move = "DISCONNECT";
      return true;
    }

    // Если bytes_read < 0 и errno == EAGAIN, это значит, что данных в сети
    // просто еще нет
    return false;
  }

  bool isConnected() const { return is_connected; }

  void disconnect() {
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

private:
  // Перевод дескриптора сокета в неблокирующий режим через fcntl
  void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
};

#endif
