#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>

#include "raylib.h"

#define BOARD_SIZE 9

#define BOARD_GET(state, x, y) ((state)->board[(y) * BOARD_SIZE + (x)])
#define BOARD_SET(state, x, y, val) \
  ((state)->board[(y) * BOARD_SIZE + (x)] = (val))

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
} Action;

typedef struct {
  int windowWidth;
  int windowHeight;
  uint64_t simulationStep;

  CellState board[BOARD_SIZE * BOARD_SIZE];
  Action actions[BOARD_SIZE * BOARD_SIZE];
  int actionCount;

  BoardLayout boardLayout;
} State;

State *createState(void);

void init(State *state);
void doSimulation(State *state);
void draw(State *state);
#endif