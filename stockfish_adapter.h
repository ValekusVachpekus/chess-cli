#ifndef STOCKFISH_ADAPTER_H
#define STOCKFISH_ADAPTER_H

#include "chess_engine.h"
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <thread>
#include <chrono>
#include <signal.h>

class StockfishAdapter : public ChessEngine {
private:
    pid_t process_pid = -1;
    int stdin_fd = -1;
    int stdout_fd = -1;
    bool available = false;

    /**
     * Write a command to Stockfish stdin
     */
    bool writeCommand(const string &command) {
        if (stdin_fd < 0) {
            return false;
        }
        string cmd = command + "\n";
        ssize_t bytes_written =
            write(stdin_fd, cmd.c_str(), cmd.length());
        if (bytes_written < 0) {
            cerr << "Failed to write to Stockfish stdin" << endl;
            return false;
        }
        return true;
    }

    /**
     * Read a line from Stockfish stdout
     */
    string readLine(int timeout_ms = 60000) {
        if (stdout_fd < 0) {
            return "";
        }

        string result;
        char buffer[1];
        auto start = chrono::high_resolution_clock::now();

        while (true) {
            auto now = chrono::high_resolution_clock::now();
            auto elapsed =
                chrono::duration_cast<chrono::milliseconds>(now - start)
                    .count();
            if (elapsed > timeout_ms) {
                cerr << "Timeout reading from Stockfish" << endl;
                return "";
            }

            ssize_t bytes_read = read(stdout_fd, buffer, 1);
            if (bytes_read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                cerr << "Error reading from Stockfish stdout" << endl;
                return "";
            }
            if (bytes_read == 0) {
                cerr << "Stockfish closed the connection (EOF)" << endl;
                return "";
            }

            if (buffer[0] == '\r') {
                continue;
            }
            if (buffer[0] == '\n') {
                return result;
            }
            result += buffer[0];
        }
    }

    /**
     * Wait for a specific response from Stockfish
     */
    bool waitForResponse(const string &expected, int timeout_ms = 60000) {
        auto start = chrono::high_resolution_clock::now();

        while (true) {
            if (process_pid > 0) {
                int status = 0;
                pid_t res = waitpid(process_pid, &status, WNOHANG);
                if (res == process_pid) {
                    cerr << "Stockfish process exited before responding" << endl;
                    return false;
                }
            }
            auto now = chrono::high_resolution_clock::now();
            auto elapsed =
                chrono::duration_cast<chrono::milliseconds>(now - start)
                    .count();
            if (elapsed > timeout_ms) {
                cerr << "Timeout waiting for: " << expected << endl;
                return false;
            }

            string line = readLine(timeout_ms - elapsed);
            if (line.empty()) {
                continue;
            }
            if (line.find(expected) != string::npos) {
                return true;
            }
        }
    }

public:
    virtual ~StockfishAdapter() { shutdown(); }

    bool initialize() override {
        if (available) {
            return true;
        }

        int stdin_pipe[2], stdout_pipe[2];

        if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
            cerr << "Failed to create pipes" << endl;
            return false;
        }

        process_pid = fork();
        if (process_pid < 0) {
            cerr << "Failed to fork Stockfish process" << endl;
            return false;
        }

        if (process_pid == 0) {
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);

            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);

            close(stdin_pipe[0]);
            close(stdout_pipe[1]);

            const char *stockfish_path = getenv("STOCKFISH_PATH");
            if (stockfish_path != nullptr && stockfish_path[0] != '\0') {
                execl(stockfish_path, "stockfish", nullptr);
            } else {
                execlp("stockfish", "stockfish", nullptr);
            }
            cerr << "Failed to exec stockfish (set STOCKFISH_PATH or install stockfish in PATH)" << endl;
            exit(1);
        }

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        stdin_fd = stdin_pipe[1];
        stdout_fd = stdout_pipe[0];

        if (!writeCommand("uci")) {
            cerr << "Failed to send uci command" << endl;
            shutdown();
            return false;
        }

        if (!waitForResponse("uciok")) {
            cerr << "Stockfish did not respond to uci command" << endl;
            shutdown();
            return false;
        }

        if (!writeCommand("isready")) {
            cerr << "Failed to send isready command" << endl;
            shutdown();
            return false;
        }

        if (!waitForResponse("readyok")) {
            cerr << "Stockfish did not respond to isready command" << endl;
            shutdown();
            return false;
        }

        available = true;
        cout << "Stockfish initialized successfully" << endl;
        return true;
    }

    string getBestMove(const string &fen, int movetime_ms) override {
        if (!available) {
            cerr << "Stockfish not available" << endl;
            return "";
        }

        string pos_cmd = "position fen " + fen;
        if (!writeCommand(pos_cmd)) {
            cerr << "Failed to send position command" << endl;
            available = false;
            return "";
        }

        string go_cmd = "go movetime " + to_string(movetime_ms);
        if (!writeCommand(go_cmd)) {
            cerr << "Failed to send go command" << endl;
            available = false;
            return "";
        }

        while (true) {
            string line = readLine(movetime_ms + 1000);
            if (line.empty()) {
                cerr << "Failed to get move from Stockfish" << endl;
                available = false;
                return "";
            }

            if (line.find("bestmove") != string::npos) {
                size_t move_pos = line.find("bestmove") + 9;
                size_t move_end = line.find(" ", move_pos);
                if (move_end == string::npos) {
                    move_end = line.length();
                }
                string move =
                    line.substr(move_pos, move_end - move_pos);

                return move;
            }
        }
    }

    bool isAvailable() override { return available; }

    void shutdown() override {
        if (stdin_fd >= 0) {
            close(stdin_fd);
            stdin_fd = -1;
        }
        if (stdout_fd >= 0) {
            close(stdout_fd);
            stdout_fd = -1;
        }
        if (process_pid > 0) {
            kill(process_pid, SIGTERM);
            int status;
            waitpid(process_pid, &status, 0);
            process_pid = -1;
        }
        available = false;
    }
};

#endif // STOCKFISH_ADAPTER_H
