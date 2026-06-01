## Chess engine
Written in C++, source code of engine is `gameboard.cpp`.
`gameboard.cpp` is also a CLI version of chess. Controls are using commands (e.g. e2 e4). \
Compilation: `g++ gameboard.cpp -o gameboard`. \
Using: `./gameboard`. \
<img width="1128" height="274" alt="image" src="https://github.com/user-attachments/assets/1e26e1b7-8a82-4e5f-86fa-e7d3f7ea1722" />


## TUI version
`tui.cpp` is a TUI version of game. Controls are arrows or VIM keys. It also can rotate board depending on player's side. \
Compilation: `g++ tui.cpp -lncurses -o chess-tui`. \
Using: `./chess-tui` \
There are hot keys of TUI version:
 - f - flip gameboard when turn
 - c - center the gameboard
 - h - hide interface
 - s - save game
 - o - load game
<img width="963" height="340" alt="image" src="https://github.com/user-attachments/assets/470df939-f4be-4749-8d99-8d6dc260015d" />


## CLI Options

The TUI version supports standard GNU/POSIX command-line arguments for quick configuration directly from your shell:

### Game Options
* `-m, --mode <mode>`
    Set the game mode. Available options:
    * `human` — Human vs Human (local multiplayer).
    * `white` — Human plays White, bot plays Black.
    * `black` — Human plays Black, bot plays White.
    * `auto` — Bot vs Bot simulation.
    * `replay` — Replay of a saved game with Stockfish feedback.
* `-t, --time <ms>`
    Set the bot movetime in milliseconds (default: 200).

### Interface Options
* `-f, --flip`
    Flip the board at startup so Black pieces are on the bottom.
* `-c, --center`
    Center the board dynamically inside your terminal window.
* `-H, --hide-ui`
    No UI mode. Hide all helper UI text, engine logs, and status bars, showing only the chess board.
* `-m, --mode <mode>`
    Chose game mode.
To see more, use `chess-tui -h`.

### Generic Options
* `-h, --help`
    Show the help message with available options.
* `-v, --version`
    Show version information.

Example usage:
```bash
./chess-tui --mode black --time 500 --center --hide-ui
```

## Stockfish usage

Game uses Stockfish via UCI.
1. Install Stockfish:
`sudo pacman -S stockfish`

2. Usage 
When game is launched without CLI arguments, there are 4 game modes available via menu selection:

* h - human vs human
* w - bot plays white
* b - bot plays black
* a - bot vs bot
Also you can chose movetime (default 200ms). More movetime makes bot smarter.

## Multiplayer mode

Game supports local multiplayer via UCI.
1. Start server (Plays White):
`chess-tui -m human -S`

2. Connect to a server (Plays Black):
`chess-tui -m human -C <IP (default 127.0.0.1)> -P <Port (default 8888)>`

## Replay mode

Game supports replay of game with stockfish feedback.
1. Save game, using `s`.
2. Launch game in `replay` mode using TUI or `chess-tui -m replay`.
3. Type your game save path.
4. Use arrows to move turns and `E` to evaluate move and best move using stockfish.
<img width="960" height="354" alt="image" src="https://github.com/user-attachments/assets/9057dbc6-8033-4f0f-aaa8-dc6f5c11b644" />


## TODO
* [x] Add Stockfish.
* [x] Add 2 player game via 2 screens or localhost.
* [ ] Add web GUI interface.

## LICENSE

Uses GNU GPL v3 [LICENSE](./LICENSE). \
Author: Ilia Shchetkov
