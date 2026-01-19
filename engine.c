#include "engine.h"
#include "raylib.h"
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include "renderer.h"

static const char *SAVE_DIR = "games";
static const int kBoardSizes[] = {9, 13, 19};
static const int kBoardSizeCount = 3;
static const int HUD_PANEL_WIDTH = 320;
static const int HUD_PANEL_HEIGHT = 220;
static const int HUD_PANEL_MARGIN = 12;

static unsigned int countLiberties(State *state, int x, int y);
static unsigned int floodFill(State *state, CellState scratchpad[NUM_CELLS], int x, int y);
static void updateBoardLayout(State *state);
static CellState getNextPlayer(State *state);
static bool applyMove(State *state, int x, int y, CellState player, bool isPass);
static void recordHistory(State *state);
static void resetGame(State *state);
static void startNewGame(State *state, int boardSize);
static void undoMove(State *state);
static void drawHud(State *state);
static Font loadUIFont(bool *hasCustomFont);
static bool saveGame(const State *state, const char *path);
static bool loadGame(State *state, const char *path);
static void formatCoord(char *out, size_t outSize, int x, int y);
static void drawGhostStone(State *state);
static void drawMenu(State *state);
static void drawSavePicker(State *state);
static bool ensureSaveDirectory(void);
static void generateSavePath(State *state);
static float stoneRadius(const State *state);
static int boardSizeIndexFor(int size);
static void openSavePicker(State *state);
static void closeSavePicker(State *state);
static void refreshSavePicker(State *state);
static int compareSaveEntries(const void *a, const void *b);

State *createState(void)
{
    State *state = calloc(1, sizeof(State));
    state->windowWidth = 800;
    state->windowHeight = 600;

    return state;
}

static bool isInBounds(const State *state, int x, int y)
{
    return x >= 0 && x < state->boardSize && y >= 0 && y < state->boardSize;
}

// Returns true if there is a last action, false otherwise
static bool getLastAction(State *state, Action *action)
{
    if (state->actionCount > 0)
    {
        *action = state->actions[state->actionCount - 1];
        return true;
    }

    return false;
}

void init(State *state)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(state->windowWidth, state->windowHeight, "Game of Go");

    SetTargetFPS(60);

    state->selectedBoardSizeIndex = 0;
    state->boardSize = kBoardSizes[state->selectedBoardSizeIndex];
    state->inNewGameMenu = true;
    state->autoSaveEnabled = true;
    state->savePath[0] = '\0';
    resetGame(state);
    updateBoardLayout(state);
    state->uiFont = loadUIFont(&state->hasCustomFont);
}

static void updateBoardLayout(State *state)
{
    // Get current window dimensions (handles resize)
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();

    // Calculate the biggest square that fits with padding on all sides
    int availableWidth = windowWidth - 2 * BOARD_PADDING;
    int availableHeight = windowHeight - 2 * BOARD_PADDING;
    int squareSize = fmax(0, fmin(availableWidth, availableHeight));
    int sizeWithRightHud = fmin(availableHeight, availableWidth - HUD_PANEL_WIDTH - HUD_PANEL_MARGIN);
    int sizeWithBottomHud = fmin(availableWidth, availableHeight - HUD_PANEL_HEIGHT - HUD_PANEL_MARGIN);

    if (sizeWithRightHud > 0 || sizeWithBottomHud > 0)
    {
        squareSize = fmax(sizeWithRightHud, sizeWithBottomHud);
    }

    if (squareSize < 0)
    {
        squareSize = 0;
    }

    int gridSizeRaw = squareSize - 2 * GRID_PADDING;
    if (gridSizeRaw < 0)
    {
        gridSizeRaw = 0;
    }
    int cellSpacing = state->boardSize > 1 ? gridSizeRaw / (state->boardSize - 1) : gridSizeRaw;
    if (cellSpacing < 1)
    {
        cellSpacing = 1;
    }
    int gridSize = cellSpacing * (state->boardSize - 1);
    int boardSizePx = gridSize + 2 * GRID_PADDING;

    int x = (windowWidth - boardSizePx) / 2;
    int y = (windowHeight - boardSizePx) / 2;
    int gridX = x + GRID_PADDING;
    int gridY = y + GRID_PADDING;

    state->boardLayout.gridX = gridX;
    state->boardLayout.gridY = gridY;
    state->boardLayout.gridSize = gridSize;
    state->boardLayout.cellSpacing = cellSpacing;
    state->boardLayout.boardSize = boardSizePx;
}

static void screenToBoardCoordinates(State *state, int screenX, int screenY, int *boardX, int *boardY)
{
    // Calculate which intersection is closest to the click
    int closestX = (screenX - state->boardLayout.gridX + state->boardLayout.cellSpacing / 2) / state->boardLayout.cellSpacing;
    int closestY = (screenY - state->boardLayout.gridY + state->boardLayout.cellSpacing / 2) / state->boardLayout.cellSpacing;
    
    // Check if the click is within the stone hitbox around the intersection
    int intersectionScreenX = state->boardLayout.gridX + closestX * state->boardLayout.cellSpacing;
    int intersectionScreenY = state->boardLayout.gridY + closestY * state->boardLayout.cellSpacing;
    
    int hitRadius = (int)stoneRadius(state); // Slightly smaller than half cell spacing
    int dx = screenX - intersectionScreenX;
    int dy = screenY - intersectionScreenY;
    
    if (dx * dx + dy * dy <= hitRadius * hitRadius) {
        *boardX = closestX;
        *boardY = closestY;
    } else {
        *boardX = -1;
        *boardY = -1;
    }

    if (*boardX < 0 || *boardX >= state->boardSize || *boardY < 0 || *boardY >= state->boardSize)
    {
        *boardX = -1;
        *boardY = -1;
    }
}

static void recordHistory(State *state)
{
    int index = state->actionCount;
    if (index < 0 || index > MAX_ACTIONS)
    {
        return;
    }

    memcpy(state->historyBoards[index], state->board, sizeof(state->board));
    memcpy(state->koHistory[index], state->koBoard, sizeof(state->koBoard));
    state->hasKoHistory[index] = state->hasKoBoard;
    state->captureHistoryBlack[index] = state->capturesBlack;
    state->captureHistoryWhite[index] = state->capturesWhite;
}

static void resetGame(State *state)
{
    memset(state->board, CELL_EMPTY, sizeof(state->board));
    memset(state->koBoard, CELL_EMPTY, sizeof(state->koBoard));
    state->hasKoBoard = false;
    state->actionCount = 0;
    state->capturesBlack = 0;
    state->capturesWhite = 0;
    state->reviewMode = false;
    state->reviewIndex = 0;
    recordHistory(state);
}

static bool ensureSaveDirectory(void)
{
    struct stat st = {0};
    if (stat(SAVE_DIR, &st) == 0)
    {
        return S_ISDIR(st.st_mode);
    }

#if defined(_WIN32)
    return _mkdir(SAVE_DIR) == 0;
#else
    return mkdir(SAVE_DIR, 0755) == 0;
#endif
}

static void generateSavePath(State *state)
{
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    char timestamp[64];
    if (local)
    {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H-%M-%S", local);
    }
    else
    {
        snprintf(timestamp, sizeof(timestamp), "unknown-time");
    }

    snprintf(state->savePath, sizeof(state->savePath), "%s/%s.sgf", SAVE_DIR, timestamp);
    state->gameStartTimestamp = (uint64_t)now;
}

static void startNewGame(State *state, int boardSize)
{
    state->boardSize = boardSize;
    state->selectedBoardSizeIndex = boardSizeIndexFor(boardSize);
    resetGame(state);
    generateSavePath(state);
    ensureSaveDirectory();
    saveGame(state, state->savePath);
    state->inNewGameMenu = false;
    updateBoardLayout(state);
}

static void undoMove(State *state)
{
    if (state->actionCount <= 0)
    {
        return;
    }

    state->actionCount--;
    memcpy(state->board, state->historyBoards[state->actionCount], sizeof(state->board));
    memcpy(state->koBoard, state->koHistory[state->actionCount], sizeof(state->koBoard));
    state->hasKoBoard = state->hasKoHistory[state->actionCount];
    state->capturesBlack = state->captureHistoryBlack[state->actionCount];
    state->capturesWhite = state->captureHistoryWhite[state->actionCount];

    if (state->reviewIndex > state->actionCount)
    {
        state->reviewIndex = state->actionCount;
    }

    if (state->autoSaveEnabled && state->savePath[0] != '\0')
    {
        saveGame(state, state->savePath);
    }
}

static CellState getNextPlayer(State *state)
{
    Action lastAction = {0};
    if (getLastAction(state, &lastAction))
    {
        return lastAction.cellState == CELL_BLACK ? CELL_WHITE : CELL_BLACK;
    }

    return CELL_BLACK;
}

static bool recordAction(State *state, int x, int y, CellState cellState, bool isPass)
{
    if (state->actionCount >= MAX_ACTIONS)
    {
        return false;
    }

    state->actions[state->actionCount].x = x;
    state->actions[state->actionCount].y = y;
    state->actions[state->actionCount].cellState = cellState;
    state->actions[state->actionCount].isPass = isPass;
    state->actionCount++;
    recordHistory(state);

    if (!state->reviewMode)
    {
        state->reviewIndex = state->actionCount;
    }

    if (state->autoSaveEnabled && state->savePath[0] != '\0')
    {
        saveGame(state, state->savePath);
    }

    return true;
}

static bool applyMove(State *state, int x, int y, CellState player, bool isPass)
{
    if (state->actionCount >= MAX_ACTIONS)
    {
        return false;
    }

    CellState boardBeforeMove[NUM_CELLS];
    CellState koBeforeMove[NUM_CELLS];
    bool hasKoBeforeMove = state->hasKoBoard;
    int capturesBlackBefore = state->capturesBlack;
    int capturesWhiteBefore = state->capturesWhite;

    memcpy(boardBeforeMove, state->board, sizeof(state->board));
    memcpy(koBeforeMove, state->koBoard, sizeof(state->koBoard));

    if (isPass)
    {
        memcpy(state->koBoard, boardBeforeMove, sizeof(state->board));
        state->hasKoBoard = true;
        if (!recordAction(state, -1, -1, player, true))
        {
            memcpy(state->koBoard, koBeforeMove, sizeof(state->koBoard));
            state->hasKoBoard = hasKoBeforeMove;
            return false;
        }
        return true;
    }

    if (!isInBounds(state, x, y) || BOARD_GET(state, x, y) != CELL_EMPTY)
    {
        return false;
    }

    BOARD_SET(state, x, y, player);

    CellState scratchpad[NUM_CELLS];
    memset(scratchpad, 0, sizeof(scratchpad));
    bool shouldRemove = shouldRemoveStones(state, scratchpad, x, y);
    if (shouldRemove)
    {
        removeStones(state, scratchpad, player);
    }

    if (countLiberties(state, x, y) == 0)
    {
        memcpy(state->board, boardBeforeMove, sizeof(state->board));
        state->capturesBlack = capturesBlackBefore;
        state->capturesWhite = capturesWhiteBefore;
        return false;
    }

    if (state->hasKoBoard &&
        memcmp(state->board, state->koBoard, sizeof(state->board)) == 0)
    {
        memcpy(state->board, boardBeforeMove, sizeof(state->board));
        state->capturesBlack = capturesBlackBefore;
        state->capturesWhite = capturesWhiteBefore;
        return false;
    }

    memcpy(state->koBoard, boardBeforeMove, sizeof(state->board));
    state->hasKoBoard = true;

    if (!recordAction(state, x, y, player, false))
    {
        memcpy(state->board, boardBeforeMove, sizeof(state->board));
        memcpy(state->koBoard, koBeforeMove, sizeof(state->koBoard));
        state->hasKoBoard = hasKoBeforeMove;
        state->capturesBlack = capturesBlackBefore;
        state->capturesWhite = capturesWhiteBefore;
        return false;
    }

    return true;
}

static Font loadUIFont(bool *hasCustomFont)
{
    *hasCustomFont = false;
    const char *candidates[] = {
        "assets/ui.ttf",
        "/System/Library/Fonts/Supplemental/Palatino.ttc",
        "/System/Library/Fonts/Supplemental/Georgia.ttf",
        "/System/Library/Fonts/Supplemental/Optima.ttf",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        if (FileExists(candidates[i]))
        {
            Font font = LoadFontEx(candidates[i], 24, NULL, 0);
            if (font.texture.id != 0)
            {
                *hasCustomFont = true;
                return font;
            }
        }
    }

    return GetFontDefault();
}

void doSimulation(State *state)
{
    state->simulationStep++;
    updateBoardLayout(state);

    if (state->savePickerActive)
    {
        if (IsKeyPressed(KEY_ESCAPE))
        {
            closeSavePicker(state);
        }
        if (IsKeyPressed(KEY_DOWN))
        {
            if (state->savePickerSelected + 1 < state->savePickerCount)
            {
                state->savePickerSelected++;
            }
        }
        if (IsKeyPressed(KEY_UP))
        {
            if (state->savePickerSelected > 0)
            {
                state->savePickerSelected--;
            }
        }
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && state->savePickerCount > 0)
        {
            SaveEntry entry = state->saveEntries[state->savePickerSelected];
            loadGame(state, entry.path);
            closeSavePicker(state);
        }
        return;
    }

    if (state->inNewGameMenu)
    {
        if (IsKeyPressed(KEY_LEFT))
        {
            state->selectedBoardSizeIndex = (state->selectedBoardSizeIndex + kBoardSizeCount - 1) % kBoardSizeCount;
            state->boardSize = kBoardSizes[state->selectedBoardSizeIndex];
            updateBoardLayout(state);
        }
        if (IsKeyPressed(KEY_RIGHT))
        {
            state->selectedBoardSizeIndex = (state->selectedBoardSizeIndex + 1) % kBoardSizeCount;
            state->boardSize = kBoardSizes[state->selectedBoardSizeIndex];
            updateBoardLayout(state);
        }
        if (IsKeyPressed(KEY_ONE))
        {
            state->selectedBoardSizeIndex = 0;
            state->boardSize = kBoardSizes[state->selectedBoardSizeIndex];
            updateBoardLayout(state);
        }
        if (IsKeyPressed(KEY_TWO))
        {
            state->selectedBoardSizeIndex = 1;
            state->boardSize = kBoardSizes[state->selectedBoardSizeIndex];
            updateBoardLayout(state);
        }
        if (IsKeyPressed(KEY_THREE))
        {
            state->selectedBoardSizeIndex = 2;
            state->boardSize = kBoardSizes[state->selectedBoardSizeIndex];
            updateBoardLayout(state);
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        {
            startNewGame(state, kBoardSizes[state->selectedBoardSizeIndex]);
        }
        if (IsKeyPressed(KEY_L))
        {
            openSavePicker(state);
        }
        return;
    }

    if (IsKeyPressed(KEY_N))
    {
        state->inNewGameMenu = true;
        state->reviewMode = false;
        state->reviewIndex = state->actionCount;
        return;
    }

    if (IsKeyPressed(KEY_R))
    {
        state->reviewMode = !state->reviewMode;
        state->reviewIndex = state->actionCount;
    }

    if (state->reviewMode)
    {
        if (IsKeyPressed(KEY_RIGHT))
        {
            state->reviewIndex = state->reviewIndex < state->actionCount ? state->reviewIndex + 1 : state->reviewIndex;
        }
        if (IsKeyPressed(KEY_LEFT))
        {
            state->reviewIndex = state->reviewIndex > 0 ? state->reviewIndex - 1 : 0;
        }
    }

    if (IsKeyPressed(KEY_L))
    {
        openSavePicker(state);
    }

    if (!state->reviewMode && IsKeyPressed(KEY_U))
    {
        undoMove(state);
    }

    if (!state->reviewMode && IsKeyPressed(KEY_P))
    {
        applyMove(state, -1, -1, getNextPlayer(state), true);
    }

    if (state->reviewMode)
    {
        return;
    }

    // Handle input 
    int boardX = -1, boardY = -1;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        int x = GetMouseX();
        int y = GetMouseY();

        screenToBoardCoordinates(state, x, y, &boardX, &boardY);
    }
    
    // Handle game logic 
    if (boardX != -1 && boardY != -1)
    {
        applyMove(state, boardX, boardY, getNextPlayer(state), false);
    }
}

static CellState boardAt(const CellState *board, int x, int y)
{
    return board[BOARD_INDEX(x, y)];
}

static void drawBoard(State *state, const CellState *board)
{
    int x = state->boardLayout.gridX - GRID_PADDING;
    int y = state->boardLayout.gridY - GRID_PADDING;
    DrawRectangle(x, y, state->boardLayout.boardSize, state->boardLayout.boardSize, GoBoardColor);

    // Draw vertical lines
    for (int i = 0; i < state->boardSize; i++)
    {
        DrawLineEx((Vector2){state->boardLayout.gridX + i * state->boardLayout.cellSpacing, state->boardLayout.gridY}, (Vector2){state->boardLayout.gridX + i * state->boardLayout.cellSpacing, state->boardLayout.gridY + state->boardLayout.gridSize}, LINE_THICKNESS, GridLineOverlayColor);
    }

    // Draw horizontal lines
    for (int i = 0; i < state->boardSize; i++)
    {
        DrawLineEx((Vector2){state->boardLayout.gridX, state->boardLayout.gridY + i * state->boardLayout.cellSpacing}, (Vector2){state->boardLayout.gridX + state->boardLayout.gridSize, state->boardLayout.gridY + i * state->boardLayout.cellSpacing}, LINE_THICKNESS, GridLineOverlayColor);
    }

    float radius = stoneRadius(state);
    // Draw Go stones
    for (int x = 0; x < state->boardSize; x++)
    {
        for (int y = 0; y < state->boardSize; y++)
        {
            Color *color = NULL;
            if (boardAt(board, x, y) == CELL_BLACK)
            {
                color = &BLACK;
            }
            else if (boardAt(board, x, y) == CELL_WHITE)
            {
                color = &WHITE;
            }

            if (color != NULL)
            {
                DrawCircle(state->boardLayout.gridX + x * state->boardLayout.cellSpacing,
                           state->boardLayout.gridY + y * state->boardLayout.cellSpacing,
                           radius, *color);
            }
        }
    }
}

void draw(State *state)
{
    BeginDrawing();
    ClearBackground(RAYWHITE);
    updateBoardLayout(state);
    if (state->inNewGameMenu)
    {
        drawMenu(state);
    }
    else
    {
        const CellState *boardToDraw = state->reviewMode ? state->historyBoards[state->reviewIndex] : state->board;
        drawBoard(state, boardToDraw);
        if (!state->reviewMode)
        {
            drawGhostStone(state);
        }
        drawHud(state);
    }
    if (state->savePickerActive)
    {
        drawSavePicker(state);
    }
    EndDrawing();
}

static const char *playerName(CellState player)
{
    return player == CELL_BLACK ? "Black" : "White";
}

static void formatCoord(char *out, size_t outSize, int x, int y)
{
    if (x < 0 || y < 0)
    {
        snprintf(out, outSize, "pass");
        return;
    }

    snprintf(out, outSize, "%c%d", (char)('A' + x), y + 1);
}

static void drawHud(State *state)
{
    const int panelWidth = HUD_PANEL_WIDTH;
    const int panelHeight = HUD_PANEL_HEIGHT;
    const int margin = HUD_PANEL_MARGIN;
    int panelX = margin;
    int panelY = margin;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    int boardX = state->boardLayout.gridX - GRID_PADDING;
    int boardY = state->boardLayout.gridY - GRID_PADDING;
    int boardSizePx = state->boardLayout.boardSize;
    int rightSpace = screenWidth - (boardX + boardSizePx);
    int leftSpace = boardX;
    int bottomSpace = screenHeight - (boardY + boardSizePx);
    int topSpace = boardY;

    if (rightSpace >= panelWidth + margin)
    {
        panelX = boardX + boardSizePx + margin;
        panelY = boardY;
    }
    else if (leftSpace >= panelWidth + margin)
    {
        panelX = boardX - panelWidth - margin;
        panelY = boardY;
    }
    else if (bottomSpace >= panelHeight + margin)
    {
        panelX = boardX;
        panelY = boardY + boardSizePx + margin;
    }
    else if (topSpace >= panelHeight + margin)
    {
        panelX = boardX;
        panelY = boardY - panelHeight - margin;
    }
    else
    {
        return;
    }

    if (panelX + panelWidth + margin > screenWidth)
    {
        panelX = screenWidth - panelWidth - margin;
    }
    if (panelX < margin)
    {
        panelX = margin;
    }
    if (panelY + panelHeight + margin > screenHeight)
    {
        panelY = screenHeight - panelHeight - margin;
    }
    if (panelY < margin)
    {
        panelY = margin;
    }

    const Color panelColor = (Color){245, 238, 223, 220};
    const Color borderColor = (Color){120, 90, 60, 200};
    const Color textColor = (Color){35, 25, 15, 255};

    DrawRectangle(panelX - 8, panelY - 8, panelWidth + 16, panelHeight + 16, panelColor);
    DrawRectangleLines(panelX - 8, panelY - 8, panelWidth + 16, panelHeight + 16, borderColor);

    float fontSize = 20.0f;
    float spacing = 1.0f;
    Vector2 pos = {(float)panelX, (float)panelY};
    char line[128];

    snprintf(line, sizeof(line), "Mode: %s", state->reviewMode ? "Review" : "Play");
    DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
    pos.y += fontSize + 6;

    if (state->reviewMode)
    {
        snprintf(line, sizeof(line), "Move: %d/%d", state->reviewIndex, state->actionCount);
    }
    else
    {
        snprintf(line, sizeof(line), "Turn: %s", playerName(getNextPlayer(state)));
    }
    DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
    pos.y += fontSize + 6;

    int capBlack = state->reviewMode ? state->captureHistoryBlack[state->reviewIndex] : state->capturesBlack;
    int capWhite = state->reviewMode ? state->captureHistoryWhite[state->reviewIndex] : state->capturesWhite;
    snprintf(line, sizeof(line), "Captures: B %d  W %d", capBlack, capWhite);
    DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
    pos.y += fontSize + 6;

    const char *fileName = state->savePath;
    const char *slash = strrchr(state->savePath, '/');
    if (slash)
    {
        fileName = slash + 1;
    }
    snprintf(line, sizeof(line), "Save: %s", fileName && fileName[0] ? fileName : "unset");
    DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
    pos.y += fontSize + 6;

    if (state->actionCount > 0)
    {
        int lastIndex = state->reviewMode ? state->reviewIndex - 1 : state->actionCount - 1;
        if (lastIndex >= 0)
        {
            Action last = state->actions[lastIndex];
            char coord[32];
            formatCoord(coord, sizeof(coord), last.x, last.y);
            snprintf(line, sizeof(line), "Last: %s %s", playerName(last.cellState), coord);
            DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
            pos.y += fontSize + 6;
        }
    }

    DrawTextEx(state->uiFont, "Controls: Click=play  P=pass  U=undo", pos, fontSize - 2.0f, spacing, textColor);
    pos.y += fontSize + 4;
    DrawTextEx(state->uiFont, "R=review  Left/Right=step  N=new", pos, fontSize - 2.0f, spacing, textColor);
    pos.y += fontSize + 4;
    DrawTextEx(state->uiFont, "L=load save (autosave on)", pos, fontSize - 2.0f, spacing, textColor);
}

static void drawMenu(State *state)
{
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    const int panelWidth = 460;
    const int panelHeight = 260;
    int panelX = (screenWidth - panelWidth) / 2;
    int panelY = (screenHeight - panelHeight) / 2;
    const Color panelColor = (Color){245, 238, 223, 240};
    const Color borderColor = (Color){120, 90, 60, 200};
    const Color textColor = (Color){35, 25, 15, 255};
    const Color highlight = (Color){200, 150, 90, 255};
    const Color muted = (Color){70, 60, 50, 255};

    DrawRectangle(panelX, panelY, panelWidth, panelHeight, panelColor);
    DrawRectangleLines(panelX, panelY, panelWidth, panelHeight, borderColor);

    float titleSize = 32.0f;
    float bodySize = 20.0f;
    float spacing = 1.0f;
    Vector2 pos = {(float)panelX + 24.0f, (float)panelY + 24.0f};

    DrawTextEx(state->uiFont, "New Game", pos, titleSize, spacing, textColor);
    pos.y += titleSize + 14.0f;

    DrawTextEx(state->uiFont, "Select board size", pos, bodySize, spacing, muted);
    pos.y += bodySize + 12.0f;

    for (int i = 0; i < kBoardSizeCount; i++)
    {
        int buttonWidth = 90;
        int buttonHeight = 40;
        int buttonX = panelX + 24 + i * (buttonWidth + 16);
        int buttonY = (int)pos.y;
        Color buttonColor = i == state->selectedBoardSizeIndex ? highlight : (Color){230, 214, 190, 255};
        DrawRectangle(buttonX, buttonY, buttonWidth, buttonHeight, buttonColor);
        DrawRectangleLines(buttonX, buttonY, buttonWidth, buttonHeight, borderColor);
        char label[16];
        snprintf(label, sizeof(label), "%dx%d", kBoardSizes[i], kBoardSizes[i]);
        Vector2 textSize = MeasureTextEx(state->uiFont, label, bodySize, spacing);
        DrawTextEx(state->uiFont, label,
                   (Vector2){buttonX + (buttonWidth - textSize.x) * 0.5f, buttonY + (buttonHeight - textSize.y) * 0.5f},
                   bodySize, spacing, textColor);
    }

    pos.y += 60.0f;
    DrawTextEx(state->uiFont, "Left/Right or 1/2/3 to choose", pos, bodySize - 2.0f, spacing, textColor);
    pos.y += bodySize + 6.0f;
    DrawTextEx(state->uiFont, "Enter to start  |  L to load save", pos, bodySize - 2.0f, spacing, textColor);
}

static void drawSavePicker(State *state)
{
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    const int panelWidth = 520;
    const int panelHeight = 360;
    int panelX = (screenWidth - panelWidth) / 2;
    int panelY = (screenHeight - panelHeight) / 2;
    const Color overlay = (Color){20, 15, 10, 120};
    const Color panelColor = (Color){245, 238, 223, 245};
    const Color borderColor = (Color){120, 90, 60, 200};
    const Color textColor = (Color){35, 25, 15, 255};
    const Color muted = (Color){70, 60, 50, 255};
    const Color highlight = (Color){200, 150, 90, 255};

    DrawRectangle(0, 0, screenWidth, screenHeight, overlay);
    DrawRectangle(panelX, panelY, panelWidth, panelHeight, panelColor);
    DrawRectangleLines(panelX, panelY, panelWidth, panelHeight, borderColor);

    float titleSize = 28.0f;
    float bodySize = 18.0f;
    float spacing = 1.0f;
    Vector2 pos = {(float)panelX + 20.0f, (float)panelY + 18.0f};

    DrawTextEx(state->uiFont, "Load Saved Game", pos, titleSize, spacing, textColor);
    pos.y += titleSize + 10.0f;

    if (state->savePickerCount == 0)
    {
        DrawTextEx(state->uiFont, "No saved games found in /games", pos, bodySize, spacing, muted);
        pos.y += bodySize + 10.0f;
    }
    else
    {
        int listX = (int)pos.x;
        int listY = (int)pos.y;
        int listWidth = panelWidth - 40;
        int listHeight = panelHeight - 110;
        int rowHeight = 24;
        int maxRows = listHeight / rowHeight;
        int startIndex = 0;
        if (state->savePickerSelected >= maxRows)
        {
            startIndex = state->savePickerSelected - maxRows + 1;
        }

        for (int i = 0; i < maxRows && (startIndex + i) < state->savePickerCount; i++)
        {
            int index = startIndex + i;
            int rowY = listY + i * rowHeight;
            if (index == state->savePickerSelected)
            {
                DrawRectangle(listX, rowY, listWidth, rowHeight, highlight);
            }

            DrawTextEx(state->uiFont, state->saveEntries[index].name, (Vector2){(float)listX + 6.0f, (float)rowY + 3.0f}, bodySize, spacing, textColor);
        }
    }

    Vector2 footer = {(float)panelX + 20.0f, (float)panelY + panelHeight - 32.0f};
    DrawTextEx(state->uiFont, "Up/Down to select  Enter to load  Esc to close", footer, bodySize - 2.0f, spacing, textColor);
}

static void drawGhostStone(State *state)
{
    int mouseX = GetMouseX();
    int mouseY = GetMouseY();
    int boardX = -1;
    int boardY = -1;

    screenToBoardCoordinates(state, mouseX, mouseY, &boardX, &boardY);
    if (boardX == -1 || boardY == -1)
    {
        return;
    }

    if (BOARD_GET(state, boardX, boardY) != CELL_EMPTY)
    {
        return;
    }

    CellState player = getNextPlayer(state);
    Color baseColor = player == CELL_BLACK ? BLACK : WHITE;
    Color ghostColor = (Color){baseColor.r, baseColor.g, baseColor.b, 110};

    DrawCircle(state->boardLayout.gridX + boardX * state->boardLayout.cellSpacing,
               state->boardLayout.gridY + boardY * state->boardLayout.cellSpacing,
               stoneRadius(state), ghostColor);
}

static float stoneRadius(const State *state)
{
    float radius = state->boardLayout.cellSpacing * 0.45f;
    float maxRadius = state->boardLayout.cellSpacing * 0.5f - 2.0f;
    if (maxRadius < 3.0f)
    {
        maxRadius = 3.0f;
    }
    if (radius > maxRadius)
    {
        radius = maxRadius;
    }
    if (radius < 3.0f)
    {
        radius = 3.0f;
    }

    return radius;
}

static int boardSizeIndexFor(int size)
{
    for (int i = 0; i < kBoardSizeCount; i++)
    {
        if (kBoardSizes[i] == size)
        {
            return i;
        }
    }
    return 0;
}

static void refreshSavePicker(State *state)
{
    state->savePickerCount = 0;
    state->savePickerSelected = 0;

    DIR *dir = opendir(SAVE_DIR);
    if (!dir)
    {
        return;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL)
    {
        if (state->savePickerCount >= MAX_SAVE_ENTRIES)
        {
            break;
        }

        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 4, ".sgf") != 0)
        {
            continue;
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", SAVE_DIR, name);
        struct stat st = {0};
        if (stat(path, &st) != 0)
        {
            continue;
        }
        if (!S_ISREG(st.st_mode))
        {
            continue;
        }

        SaveEntry *slot = &state->saveEntries[state->savePickerCount];
        snprintf(slot->path, sizeof(slot->path), "%s", path);
        snprintf(slot->name, sizeof(slot->name), "%s", name);
        slot->mtime = (uint64_t)st.st_mtime;
        state->savePickerCount++;
    }

    closedir(dir);

    if (state->savePickerCount > 1)
    {
        qsort(state->saveEntries, (size_t)state->savePickerCount, sizeof(SaveEntry), compareSaveEntries);
    }
}

static int compareSaveEntries(const void *a, const void *b)
{
    const SaveEntry *left = (const SaveEntry *)a;
    const SaveEntry *right = (const SaveEntry *)b;
    if (left->mtime == right->mtime)
    {
        return strcmp(left->name, right->name);
    }
    return left->mtime > right->mtime ? -1 : 1;
}

static void openSavePicker(State *state)
{
    refreshSavePicker(state);
    state->savePickerActive = true;
}

static void closeSavePicker(State *state)
{
    state->savePickerActive = false;
}

static bool saveGame(const State *state, const char *path)
{
    ensureSaveDirectory();
    FILE *file = fopen(path, "w");
    if (!file)
    {
        return false;
    }

    fprintf(file, "(;GM[1]FF[4]SZ[%d]KM[6.5]AP[game-of-go]\n", state->boardSize);
    int movesOnLine = 0;
    for (int i = 0; i < state->actionCount; i++)
    {
        Action action = state->actions[i];
        char coord[3] = {0};
        if (!action.isPass && action.x >= 0 && action.y >= 0)
        {
            coord[0] = (char)('a' + action.x);
            coord[1] = (char)('a' + action.y);
        }
        fprintf(file, ";%c[%s]", action.cellState == CELL_BLACK ? 'B' : 'W', coord);
        movesOnLine++;
        if (movesOnLine >= 10)
        {
            fprintf(file, "\n");
            movesOnLine = 0;
        }
    }
    fprintf(file, ")\n");
    fclose(file);
    return true;
}

static bool parseBoardSize(const char *data, int *outSize)
{
    const char *sz = strstr(data, "SZ[");
    if (!sz)
    {
        return false;
    }

    sz += 3;
    int size = 0;
    while (*sz && isdigit((unsigned char)*sz))
    {
        size = size * 10 + (*sz - '0');
        sz++;
    }

    if (size <= 0)
    {
        return false;
    }

    *outSize = size;
    return true;
}

static bool loadGame(State *state, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
    {
        return false;
    }

    bool success = false;
    bool previousAutoSave = state->autoSaveEnabled;
    char *data = NULL;

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0)
    {
        fclose(file);
        return false;
    }

    data = malloc((size_t)length + 1);
    if (!data)
    {
        fclose(file);
        return false;
    }

    size_t readBytes = fread(data, 1, (size_t)length, file);
    fclose(file);
    data[readBytes] = '\0';

    int sizeFromFile = state->boardSize;
    if (parseBoardSize(data, &sizeFromFile))
    {
        if (sizeFromFile > BOARD_MAX_SIZE)
        {
            goto cleanup;
        }
        state->boardSize = sizeFromFile;
        state->selectedBoardSizeIndex = boardSizeIndexFor(sizeFromFile);
        updateBoardLayout(state);
    }

    state->autoSaveEnabled = false;
    resetGame(state);
    if (path && path[0] != '\0')
    {
        snprintf(state->savePath, sizeof(state->savePath), "%s", path);
    }
    const char *cursor = data;
    while ((cursor = strchr(cursor, ';')) != NULL)
    {
        cursor++;
        if ((*cursor == 'B' || *cursor == 'W') && cursor[1] == '[')
        {
            CellState player = *cursor == 'B' ? CELL_BLACK : CELL_WHITE;
            cursor += 2;
            bool isPass = true;
            int x = -1;
            int y = -1;

            if (*cursor != ']')
            {
                if (cursor[0] && cursor[1])
                {
                    x = cursor[0] - 'a';
                    y = cursor[1] - 'a';
                    isPass = false;
                }
            }

            const char *end = strchr(cursor, ']');
            if (!end)
            {
                goto cleanup;
            }
            cursor = end;

            if (!isPass && (!isInBounds(state, x, y)))
            {
                goto cleanup;
            }

            if (!applyMove(state, x, y, player, isPass))
            {
                goto cleanup;
            }
        }
    }

    state->reviewMode = false;
    state->reviewIndex = state->actionCount;
    state->inNewGameMenu = false;
    success = true;

cleanup:
    state->autoSaveEnabled = previousAutoSave;
    if (data)
    {
        free(data);
    }
    return success;
}


static unsigned int countLiberties(State *state, int x, int y) {
    if (!isInBounds(state, x, y))
    {
        return 0;
    }

    CellState color = BOARD_GET(state, x, y);
    if (color == CELL_EMPTY)
    {
        return 0;
    }

    bool visited[NUM_CELLS] = {0};
    bool libertySeen[NUM_CELLS] = {0};
    int stackX[NUM_CELLS];
    int stackY[NUM_CELLS];
    int stackSize = 0;

    int startIndex = BOARD_INDEX(x, y);
    visited[startIndex] = true;
    stackX[stackSize] = x;
    stackY[stackSize] = y;
    stackSize++;

    unsigned int liberties = 0;
    while (stackSize > 0)
    {
        stackSize--;
        int cx = stackX[stackSize];
        int cy = stackY[stackSize];

        const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int i = 0; i < 4; i++)
        {
            int nx = cx + offsets[i][0];
            int ny = cy + offsets[i][1];
            if (!isInBounds(state, nx, ny))
            {
                continue;
            }

            int nIndex = BOARD_INDEX(nx, ny);
            CellState neighbor = BOARD_GET(state, nx, ny);
            if (neighbor == CELL_EMPTY)
            {
                if (!libertySeen[nIndex])
                {
                    libertySeen[nIndex] = true;
                    liberties++;
                }
                continue;
            }

            if (neighbor != color || visited[nIndex])
            {
                continue;
            }

            visited[nIndex] = true;
            stackX[stackSize] = nx;
            stackY[stackSize] = ny;
            stackSize++;
        }
    }

    return liberties;
}

static unsigned int floodFill(State *state, CellState scratchpad[NUM_CELLS], int x, int y)
{ 
    if (!isInBounds(state, x, y))
    {
        return 0;
    }

    CellState color = BOARD_GET(state, x, y);
    if (color == CELL_EMPTY)
    {
        return 0;
    }

    int startIndex = BOARD_INDEX(x, y);
    if (scratchpad[startIndex] == color)
    {
        return 0;
    }

    bool visited[NUM_CELLS] = {0};
    int stackX[NUM_CELLS];
    int stackY[NUM_CELLS];
    int stackSize = 0;

    visited[startIndex] = true;
    stackX[stackSize] = x;
    stackY[stackSize] = y;
    stackSize++;

    unsigned int filled = 0;
    while (stackSize > 0)
    {
        stackSize--;
        int cx = stackX[stackSize];
        int cy = stackY[stackSize];
        int cIndex = BOARD_INDEX(cx, cy);

        scratchpad[cIndex] = color;
        filled++;

        const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int i = 0; i < 4; i++)
        {
            int nx = cx + offsets[i][0];
            int ny = cy + offsets[i][1];
            if (!isInBounds(state, nx, ny))
            {
                continue;
            }

            int nIndex = BOARD_INDEX(nx, ny);
            if (visited[nIndex])
            {
                continue;
            }

            if (BOARD_GET(state, nx, ny) != color)
            {
                continue;
            }

            visited[nIndex] = true;
            stackX[stackSize] = nx;
            stackY[stackSize] = ny;
            stackSize++;
        }
    }

    return filled;
}

bool shouldRemoveStones(State *state,
                        CellState scratchpad[NUM_CELLS], int targetX,
                        int targetY)
{
    if (!isInBounds(state, targetX, targetY))
    {
        return false;
    }

    CellState actingColor = BOARD_GET(state, targetX, targetY);
    if (actingColor == CELL_EMPTY)
    {
        return false;
    }

    CellState opponentColor = actingColor == CELL_BLACK ? CELL_WHITE : CELL_BLACK;
    bool removed = false;
    const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (int i = 0; i < 4; i++)
    {
        int nx = targetX + offsets[i][0];
        int ny = targetY + offsets[i][1];
        if (!isInBounds(state, nx, ny))
        {
            continue;
        }

        if (BOARD_GET(state, nx, ny) != opponentColor)
        {
            continue;
        }

        if (countLiberties(state, nx, ny) == 0)
        {
            if (floodFill(state, scratchpad, nx, ny) > 0)
            {
                removed = true;
            }
        }
    }

    return removed;
}

// removeStones just applies the removal of stones from scratpad to board if
// necessary
void removeStones(State *state, CellState scratpad[NUM_CELLS], CellState actingColor)
{
    int removed = 0;
    for (int i = 0; i < NUM_CELLS; i++)
    {
        if (scratpad[i] != CELL_EMPTY)
        {
            state->board[i] = CELL_EMPTY;
            removed++;
        }
    }

    if (removed > 0)
    {
        if (actingColor == CELL_BLACK)
        {
            state->capturesBlack += removed;
        }
        else if (actingColor == CELL_WHITE)
        {
            state->capturesWhite += removed;
        }
    }
}
