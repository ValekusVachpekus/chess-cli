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

## Stockfish usage 
Game uses Stockfish via UCI.
1. Install Stockfish: \
`sudo pacman -S stockfish`\
2. Usage\
When game is launched there are 4 game modes: \
 - h - human vs human
 - w - bot plays white
 - b - bot plays black 
 - a - bot vs bot 
Also you can chose movetime (default 200ms). More movetime makes bot smarter.

## TODO
 - [x] Add Stockfish.
 - [ ] Add 2 player game via 2 screens or localhost.
 - [ ] Add web GUI interface.

Author: Ilia Shchetkov
