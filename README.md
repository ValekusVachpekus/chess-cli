## Chess engine
Written in C++, source code of engine is `gameboard.cpp`.
`gameboard.cpp` is also a CLI version of chess. Controls are using commands (e.g. e2 e4). \
Compilation: `g++ gameboard.cpp -o gameboard`. \
Using: `./gameboard`. \
<img width="1128" height="274" alt="image" src="https://github.com/user-attachments/assets/1e26e1b7-8a82-4e5f-86fa-e7d3f7ea1722" />


## TUI version
`tui.cpp` is a TUI version of game. Controls are arrows or VIM-keys. It also can rotate board depending on player's side. \
Compilation: `g++ tui.cpp -lncurses -lcurl -o chess-tui` (needs libcurl for the chess.com import and Lichess play; the bundled `json.hpp` is header-only). \
Using: `./chess-tui` \
There are hot keys of TUI version:
 - f - toggle auto-flip (board flips to the side whose turn it is)
 - c - center the gameboard
 - i - hide interface
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
    * `replay` — Replay of a saved game with Stockfish feedback (press `a` to analyse).
    * `chesscom` — Import a game from chess.com by username, then replay/analyse it.
    * `lichess` — Play a live game on lichess.org through the Board API (needs a token, see below).
* `-t, --time <ms>`
    Set the bot movetime in milliseconds (default: 200).

### Interface Options
* `-f, --flip`
    Flip the board at startup so Black pieces are on the bottom.
* `-c, --center`
    Center the board dynamically inside your terminal window.
* `-H, --hide-ui`
    No UI mode. Hide all helper UI text, engine logs, and status bars, showing only the chess board.
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

## Multiplayer Mode

The game supports automatic local network match discovery (LAN Discovery via UDP Broadcast) as well as direct TCP connections.

### 1. LAN Discovery (Automatic)
Use this mode if both devices are on the same local network, a mobile hotspot, or a shared virtual LAN (e.g., Tailscale or ZeroTier):

1. **Host (Plays White):** Launch the game, pick an icon set, and select `3. Network: Create Game (Host)` from the main menu. The app will display a waiting status and start broadcasting discovery beacons in the background.
2. **Client (Plays Black):** Launch the game, pick an icon set, and select `4. Network: Find Local Games (Join)` from the main menu. The scanner will look for active hosts on the network and show them in a dynamic list. Press **Enter** on the discovered host to connect instantly.

> **If discovery finds nothing** while both devices are on the same Wi-Fi, the
> router is most likely blocking UDP broadcasts (AP/Client Isolation, common on
> guest and public networks), or a firewall is dropping the traffic. Use the
> manual connection below — the host screen now prints its IP address(es) and
> port for you to type in.

<img width="303" height="175" alt="image" src="https://github.com/user-attachments/assets/240e813f-1119-42e9-930b-8a360b4a4f24" />

---

### 2. Manual Connection (CLI Flags)
To skip the interactive menus and establish a direct connection from the terminal.
The host's waiting screen shows the exact `IP -P PORT` to enter. **Use the same
`-P` port on both sides** (the default in code is `8888`):

* **Start Host (Plays White):**
  ```bash
  ./chess-tui -S -P 8888
  ```

* **Connect to Host (Plays Black):**
  ```bash
  ./chess-tui -C <IP_ADDRESS> -P 8888
  ```



<img width="1920" height="480" alt="image" src="https://github.com/user-attachments/assets/b5f63487-420f-43e8-a7ce-addaae2216c9" />


## Replay mode

Game supports replay of game with stockfish feedback.
1. Save game, using `s`.
2. Launch game in `replay` mode using TUI or `chess-tui -m replay`.
3. Type your game save path.
4. Use arrows to move turns and `E` to evaluate move and best move using stockfish.
<img width="960" height="354" alt="image" src="https://github.com/user-attachments/assets/9057dbc6-8033-4f0f-aaa8-dc6f5c11b644" />


## Analysis mode

Inside replay mode you can analyse the whole game like the chess.com review:
1. Load a game in `replay` mode (from a save file) or import one from chess.com.
2. Press `a`. You will be asked for the per-move thinking time in milliseconds
   (a longer time is more accurate but slower; ~40 moves at 300 ms ≈ 12 s).
3. Stockfish evaluates every position once (a progress bar is shown), then a
   side panel appears next to the board.
4. Browse with the arrow keys. For each move the panel shows the move played,
   the engine's best move, the evaluation before → after, the centipawn loss
   and a colour-coded quality badge:
   **Book** (opening theory), **Best/Excellent**, **Good**, **Inaccuracy ?!**,
   **Mistake ?**, **Blunder ??**.

## Import from chess.com

Pull a real game straight from chess.com and replay/analyse it:
1. Launch the game and select `6. Online: Import from chess.com` in the menu,
   or run `chess-tui -m chesscom`.
2. Type a chess.com username. The latest monthly archive is downloaded and the
   most recent games (standard chess only) are listed with players, result,
   time class and date.
3. Pick a game — it loads into replay mode. Use the arrow keys to browse and
   press `a` to analyse it.

> Requires libcurl at build time (`-lcurl`) and internet access at runtime. The
> chess.com public API needs no account or API key.

## Play on Lichess

Play live games against real opponents on lichess.org through the official
[Board API](https://lichess.org/api#tag/Board):

1. Create a **personal API token** at
   [lichess.org/account/oauth/token](https://lichess.org/account/oauth/token)
   with the **`board:play`** scope.
2. Provide the token in one of three ways (checked in this order):
   * the `LICHESS_TOKEN` environment variable:
     ```bash
     LICHESS_TOKEN=lip_xxxxxxxx ./chess-tui -m lichess   # fish: env LICHESS_TOKEN=… ./chess-tui -m lichess
     ```
   * a **`.env`** file in the working directory with a line
     `LICHESS_TOKEN=lip_xxxxxxxx` (git-ignored);
   * otherwise the app prompts for it (input hidden) and offers to save it to
     `.env` so it is remembered next time.

   You can also pick `7. Online: Play on Lichess` from the main menu.
3. Choose how to find an opponent:
   * **Quick seek** — auto-pairing; pick minutes per side and increment.
   * **Challenge a player** — enter a username and time control.
   * **Wait for an incoming challenge** — accept a challenge sent to you.
4. The board is oriented to your colour, opponent moves stream in live, your
   moves are sent to Lichess, and both clocks are shown in the status line.
   Press **`R`** to resign (the game also ends when the server reports
   mate/resign/timeout/draw). Pawn promotion uses the usual selector.

> Requires libcurl at build time (`-lcurl`) and internet access at runtime.
> The token is only used to talk to lichess.org and is never persisted.


## TODO
* [x] Add Stockfish.
* [x] Add 2 player game via 2 screens or localhost.
* [ ] Add web GUI interface.

## LICENSE

Uses GNU GPL v3 [LICENSE](./LICENSE). \
Author: Ilia Shchetkov
