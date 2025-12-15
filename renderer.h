#ifndef RENDERER_H
#define RENDERER_H

#include "raylib.h"

#define BOARD_PADDING 48
#define GRID_PADDING 32

#define STONE_RADIUS 22

const float LINE_THICKNESS = 2.0f;

// Light tan color for Go board
static const Color GoBoardColor = {222, 184, 135, 255};

// Semi-transparent black overlay for grid lines
static const Color GridLineOverlayColor = {0, 0, 0, 40};

#endif