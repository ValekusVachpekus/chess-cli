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
<img width="787" height="331" alt="изображение" src="https://github.com/user-attachments/assets/ce491dfd-ac9a-4fb4-a027-baff90dba63a" />


## TODO
 - [ ] Add Stockfish.
 - [ ] Add 2 player game via 2 screens or localhost.
 - [ ] Add web GUI interface.

Author: Ilia Shchetkov
