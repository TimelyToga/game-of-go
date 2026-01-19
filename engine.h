#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>
#include <stdbool.h>

#include "raylib.h"

#define BOARD_MAX_SIZE 19

#define NUM_CELLS BOARD_MAX_SIZE *BOARD_MAX_SIZE
#define MAX_ACTIONS 512
#define MAX_SAVE_ENTRIES 64

#define BOARD_INDEX(x, y) ((y) * BOARD_MAX_SIZE + (x))
#define BOARD_GET(state, x, y) ((state)->board[BOARD_INDEX((x), (y))])
#define BOARD_SET(state, x, y, val) \
  ((state)->board[BOARD_INDEX((x), (y))] = (val))

typedef enum {
  CELL_EMPTY,
  CELL_BLACK,
  CELL_WHITE,
} CellState;

typedef struct {
  int gridX;
  int gridY;
  int gridSize;
  int cellSpacing;
  int boardSize;
} BoardLayout;

typedef struct {
  int x;
  int y;
  CellState cellState;
  bool isPass;
} Action;

typedef struct {
  char path[256];
  char name[128];
  uint64_t mtime;
} SaveEntry;

typedef struct {
  int windowWidth;
  int windowHeight;
  uint64_t simulationStep;

  int boardSize;
  int selectedBoardSizeIndex;
  bool inNewGameMenu;
  CellState board[NUM_CELLS];
  CellState koBoard[NUM_CELLS];
  bool hasKoBoard;
  Action actions[MAX_ACTIONS];
  int actionCount;
  CellState historyBoards[MAX_ACTIONS + 1][NUM_CELLS];
  CellState koHistory[MAX_ACTIONS + 1][NUM_CELLS];
  bool hasKoHistory[MAX_ACTIONS + 1];
  int capturesBlack;
  int capturesWhite;
  int captureHistoryBlack[MAX_ACTIONS + 1];
  int captureHistoryWhite[MAX_ACTIONS + 1];
  bool reviewMode;
  int reviewIndex;
  Font uiFont;
  bool hasCustomFont;
  char savePath[256];
  uint64_t gameStartTimestamp;
  bool autoSaveEnabled;
  bool savePickerActive;
  int savePickerCount;
  int savePickerSelected;
  SaveEntry saveEntries[MAX_SAVE_ENTRIES];

  BoardLayout boardLayout;
} State;

State *createState(void);

void init(State *state);
void doSimulation(State *state);
void draw(State *state);

// shouldRemoveStones takes in a board, scratchpad, and target location. It
// returns true if any stones will be removed They will be marked in the
// scratchpad.
bool shouldRemoveStones(State *state, CellState scratchpad[NUM_CELLS],
                        int targetX, int targetY);

// removeStones just applies the removal of stones from scratpad to board if
// necessary
void removeStones(State *state, CellState scratpad[NUM_CELLS], CellState actingColor);
#endif
