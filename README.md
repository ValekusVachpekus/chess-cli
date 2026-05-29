## Chess engine
Written in C++, source code of engine is `gameboard.cpp`.
`gameboard.cpp` is also a CLI version of chess. Controls are using commands (e.g. e2 e4). \
Compilation: `g++ gameboard.cpp -o gameboard`. \
Using: `./gameboard`. \
<img width="411" height="691" alt="изображение" src="https://github.com/user-attachments/assets/346cfec0-ef96-4cce-93aa-e07e425970fb" />

## TUI version
`tui.cpp` is a TUI version of game. Controls are arrows or VIM keys. It also can rotate board depending on player's side. \
Compilation: `g++ tui.cpp -lncurses -o chess-tui`. \
Using: `./chess-tui` \
There are some features at TUI version:
 - f - flip gameboard when turn
 - c - center the gameboard
 - h - hide interface
<img width="787" height="331" alt="изображение" src="https://github.com/user-attachments/assets/ce491dfd-ac9a-4fb4-a027-baff90dba63a" />

## CLI Options

The TUI version supports standard GNU/POSIX command-line arguments for quick configuration directly from your shell:

### Game Options
* `-m, --mode <mode>`
    Set the game mode. Available options:
    * `human` — Human vs Human (local multiplayer).
    * `white` — Human plays White, bot plays Black.
    * `black` — Human plays Black, bot plays White.
    * `auto` — Bot vs Bot simulation.
* `-t, --time <ms>`
    Set the bot movetime in milliseconds (default: 200).

### Interface Options
* `-f, --flip`
    Flip the board at startup so Black pieces are on the bottom.
* `-c, --center`
    Center the board dynamically inside your terminal window.
* `-H, --hide-ui`
    No UI mode. Hide all helper UI text, engine logs, and status bars, showing only the chess board.

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

## TODO
* [x] Add Stockfish.
* [ ] Add 2 player game via 2 screens or localhost.
* [ ] Add web GUI interface.

Author: Ilia Shchetkov
