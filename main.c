#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <assert.h>

#define MIN(a, b) (a < b) ? (a) : (b)

int die(char *msg) {
  if (errno) {
    perror(msg);
  } else {
    printf("%s\n", msg);
  }
  exit(EXIT_FAILURE);
}


typedef struct E {
  const char *path;
  char *text;
  size_t textLen;
  char *lineBuf;
  const char *error;
  bool quit;
  size_t cursor;
  int height;
  int width;

  int lineHeight;
  int visibleLineCursor;
  int visibleLineCount;
  int visibleLineTop;

  int columnWidth;
  int columnCursor;
  int columnCount;
  int columnLeft;

  TTF_Font *font;
  SDL_Window *window;
  SDL_Renderer *renderer;
} E;


void setEditorError(E *e, const char *error) {
  e->error = error;
}

void *xalloc(size_t size) {
  void *result = malloc(size);
  if (!result) {
    die("Alloc failed");
  }
  return result;
}

void *xrealloc(void *ptr, size_t size) {
  void *result = realloc(ptr, size);
  if (!result) {
    die("Realloc failed");
  }
  return result;
}


E init(char *path) {
  FILE *file = fopen(path, "r+b");
  if (!file) {
    die("Open file failed");
  }
  if (fseek(file, 0, SEEK_END) == -1) {
    die("Failed to get file size");
  }
  long int fileSize = ftell(file);
  char *text = xalloc(fileSize + 1);
  rewind(file);
  fread(text, fileSize, 1, file);
  text[fileSize] = '\0';
  fclose(file);
  return (E) {
          .path = path,
          .height=768,
          .width=1024,
          .text = text,
          .textLen = strlen(text),
  };
}


SDL_Texture *createLineTexture(E *e, char *text, SDL_Color fg, SDL_Color bg) {
  SDL_Surface *surface = TTF_RenderText(e->font, text, fg, bg);
  if (!surface) {
    setEditorError(e, TTF_GetError());
    return 0;
  }
  SDL_Texture *texture = SDL_CreateTextureFromSurface(e->renderer, surface);
  if (!texture) {
    setEditorError(e, SDL_GetError());
    return 0;
  }
  return texture;
}


void initVisibleLines(E *e) {
  e->lineHeight = TTF_FontHeight(e->font);
  e->visibleLineCount = floor(e->height * 1.0 / e->lineHeight);

  SDL_Texture* texture = createLineTexture(e, "A", (SDL_Color){0}, (SDL_Color){0});
  int w = 0;
  int h = 0;
  SDL_QueryTexture(texture, 0, 0, &w, &h);
  e->columnWidth = w;
  e->columnCount = floor(e->width * 1.0 / e->columnWidth);
  SDL_DestroyTexture(texture);
}


bool initUI(E *e) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    setEditorError(e, SDL_GetError());
    return false;
  }
  if (TTF_Init() == -1) {
    setEditorError(e, TTF_GetError());
    return false;
  }
  e->font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 16);
  if (!e->font) {
    setEditorError(e, TTF_GetError());
    return false;
  }
  e->window = SDL_CreateWindow(e->path, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, e->width, e->height, SDL_WINDOW_SHOWN);
  if (!e->window) {
    setEditorError(e, SDL_GetError());
    return false;
  }
  e->renderer = SDL_CreateRenderer(e->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC );
  if (!e->renderer) {
    setEditorError(e, SDL_GetError());
    return false;
  }
  initVisibleLines(e);
  return true;
}


void closeEditor(E *e) {
  if (e->text) {
    free(e->text);
  }
  if (e->lineBuf) {
    free(e->lineBuf);
  }
  if (e->font) {
    TTF_CloseFont(e->font);
  }
  if (e->renderer) {
    SDL_DestroyRenderer(e->renderer);
  }
  if (e->window) {
    SDL_DestroyWindow(e->window);
  }
  TTF_Quit();
  SDL_Quit();
}


int getCursorOffsetInLine(E *e) {
  int result = 0;
  if (e->cursor > 0) {
    for (size_t i = e->cursor - 1; i <= e->cursor; i--) {
      if (e->text[i] == '\n') {
        break;
      }
      result++;
    }
  }
  return result;
}


typedef struct LineIter {
  char *text;
  int textLen;
  int lineStart;
  int lineLen;
  bool calledBefore;
  bool terminated;
} LineIter;


bool lineIterNext(LineIter *iter) {
  if (!iter->text) {
    return false;
  }
  if (iter->terminated) {
    return false;
  }
  int start = iter->calledBefore ? iter->lineStart + iter->lineLen + 1 : 0;
  iter->calledBefore = true;
  iter->lineStart = start;
  int len = 0;
  for (int i = iter->lineStart; ; i++) {
    char c = iter->text[i];
    if (c == '\n') {
      break;
    }
    if (c == '\0') {
      iter->terminated = true;
      break;
    }
    len++;
  }
  iter->lineLen = len;
  return true;
}


int getCurrentLineIndex(E *e) {
  LineIter iter = {.text = e->text, .textLen = e->textLen};
  int result = 0;
  while (lineIterNext(&iter)) {
    if (iter.lineStart <= e->cursor && e->cursor <= iter.lineStart + iter.lineLen) {
      break;
    }
    result++;
  }
  return result;
}

void renderText(E *e) {
  SDL_SetRenderDrawColor(e->renderer, 0xff, 0xff, 0xff, 0xff);
  SDL_RenderClear(e->renderer);
  int currentLine = getCurrentLineIndex(e);
  int firstLine = e->visibleLineTop;
  LineIter iter = (LineIter){.text = e->text, .textLen = e->textLen};
  int y = 0;
  SDL_Color bg = {0xff, 0xff, 0xff, 0xff};
  SDL_Color fg = {0};
  SDL_Color bgCursor = {0, 0, 0xff, 0xff};
  SDL_Color fgCursor = {0xff, 0xff, 0xff, 0xff};
  int lineNum = 0;
  while (lineIterNext(&iter)) {
    if (lineNum < firstLine) {
      lineNum++;
      continue;
    }
    // todo is it ok to create/destroy a texture for every line?

    char *text = iter.text;
    int lineLen = iter.lineLen;
    if (lineLen < e->columnLeft) {
      y += e->lineHeight;
      lineNum++;
      continue;
    }
    lineLen -= e->columnLeft;
    int lineStart = iter.lineStart + e->columnLeft;
    e->lineBuf = xrealloc(e->lineBuf, lineLen + 2); // space + \0, space allows to put cursor after line end
    strncpy(e->lineBuf, text + lineStart, lineLen);
    e->lineBuf[lineLen] = ' ';
    e->lineBuf[lineLen + 1] = '\0';

    int w = 0, h = 0;
    SDL_Rect dstRect = {0};
    if (lineNum == currentLine) {
      int cursorLineOffset = ((int)e->cursor) - lineStart;
      // draw part of line before cursor
      char ch = 0;
      SDL_Texture *texture = 0;
      if (cursorLineOffset > 0) {
        ch = e->lineBuf[cursorLineOffset];
        e->lineBuf[cursorLineOffset] = '\0';
        texture = createLineTexture(e, e->lineBuf, fg, bg);
        SDL_QueryTexture(texture, 0, 0, &w, &h);
        dstRect = (SDL_Rect){0, y, w, h};
        SDL_RenderCopy(e->renderer, texture, 0, &dstRect);
        SDL_DestroyTexture(texture);
        e->lineBuf[cursorLineOffset] = ch;
      }

      // draw char with cursor
      ch = e->lineBuf[cursorLineOffset + 1];
      e->lineBuf[cursorLineOffset + 1] = '\0';
      texture = createLineTexture(e, &e->lineBuf[cursorLineOffset], fgCursor, bgCursor);
      int widthBeforeCursor = w;
      SDL_QueryTexture(texture, 0, 0, &w, &h);
      dstRect = (SDL_Rect){widthBeforeCursor, y, w, h};
      SDL_RenderCopy(e->renderer, texture, 0, &dstRect);
      SDL_DestroyTexture(texture);
      e->lineBuf[cursorLineOffset + 1] = ch;

      // draw rest of line after cursor
      if (cursorLineOffset < lineLen) {
        texture = createLineTexture(e, &e->lineBuf[cursorLineOffset + 1], fg, bg);
        int widthAfterCursor = widthBeforeCursor + w;
        SDL_QueryTexture(texture, 0, 0, &w, &h);
        dstRect = (SDL_Rect){widthAfterCursor, y, w, h};
        SDL_RenderCopy(e->renderer, texture, 0, &dstRect);
        SDL_DestroyTexture(texture);
      }
    } else {
      SDL_Texture *texture = createLineTexture(e, e->lineBuf, fg, bg);
      SDL_QueryTexture(texture, 0, 0, &w, &h);
      dstRect = (SDL_Rect){0, y, w, h};
      SDL_RenderCopy(e->renderer, texture, 0, &dstRect);
      SDL_DestroyTexture(texture);
    }
    if (y > e->height) {
      break;
    }
    y += e->lineHeight;
    lineNum++;
  }
  SDL_RenderPresent(e->renderer);
}

void insertCharAtCursor(E *e, char c) {
  assert(0 <= e->cursor && e->cursor <= e->textLen);
  size_t newTextLen = e->textLen + 1;
  char *newText = xalloc(newTextLen + 1);
  strncpy(newText, e->text, e->cursor);
  newText[e->cursor] = c;
  strncpy(&newText[e->cursor + 1], &e->text[e->cursor], e->textLen - e->cursor);
  newText[newTextLen] = '\0';
  free(e->text);
  e->text = newText;
  e->textLen = newTextLen;
  e->cursor++;
  if (c == '\n') {
    if (e->visibleLineCursor < e->visibleLineCount - 1) {
      e->visibleLineCursor++;
    } else {
      e->visibleLineTop++;
    }
    e->columnLeft = 0;
  }
}

void deleteCharAtCursor(E *e) {
  assert(0 <= e->cursor && e->cursor <= e->textLen);
  if (e->cursor == e->textLen) {
    // cursor is at '\0' terminating the text, deleting it is noop
    return;
  }
  size_t newTextLen = e->textLen - 1;
  char *newText = xalloc(newTextLen + 1);
  strncpy(newText, e->text, e->cursor);
  size_t afterCursor = e->cursor + 1;
  if (afterCursor < e->textLen) {
    strncpy(&newText[e->cursor], &e->text[afterCursor], e->textLen - afterCursor);
  }
  newText[newTextLen] = '\0';
  free(e->text);
  e->text = newText;
  e->textLen = newTextLen;
  if (e->cursor > e->textLen) {
    e->cursor = e->textLen;
  }
}

void saveFile(E *e) {
  FILE *file = fopen(e->path, "w+b");
  if (!file) {
    die("Open file failed");
  }
  size_t written = fwrite(e->text, 1, e->textLen, file);
  fclose(file);
  if (written != e->textLen) {
    die("Write failed");
  }
}

void moveLeft(E *e) {
  if (e->cursor > 0) {
    e->cursor--;
    if (e->text[e->cursor] == '\n') {
      if (e->visibleLineCursor > 0) {
        e->visibleLineCursor--;
      } else if (e->visibleLineTop > 0) {
        e->visibleLineTop--;
      }
      int cursorOffset = getCursorOffsetInLine(e);
      if (cursorOffset >= e->columnCount) {
        e->columnLeft = cursorOffset - e->columnCount;
      }
      e->columnCursor = MIN(e->columnCount, cursorOffset);
    } else {
      if (e->columnCursor > 0) {
        e->columnCursor--;
      } else if (e->columnLeft > 0) {
        e->columnLeft--;
      }
    }
  }
}

void moveRight(E *e) {
  if (e->cursor < e->textLen) {
    if (e->text[e->cursor] == '\n') {
      if (e->visibleLineCursor < e->visibleLineCount - 1) {
        e->visibleLineCursor++;
      } else {
        e->visibleLineTop++;
      }
      e->columnCursor = 0;
      e->columnLeft = 0;
    } else {
      if (e->columnCursor < e->columnCount - 1) {
        e->columnCursor++;
      } else {
        e->columnLeft++;
      }
    }
    e->cursor++;
  }
}

void runEditor(E *e) {
  renderText(e);
  SDL_Event event;
  while (!e->quit) {
    int eventCount = 0;
    SDL_StartTextInput();
    while (SDL_PollEvent(&event)) {
      eventCount++;
      switch (event.type) {
        case SDL_QUIT:
          e->quit = true;
          break;
        case SDL_TEXTINPUT: {
          size_t textLen = strlen(event.text.text);
          for (size_t i = 0; i < textLen; i++) {
            insertCharAtCursor(e, event.text.text[i]);
          }
          break;
        }
        case SDL_KEYDOWN: {
          SDL_Keycode keySym = event.key.keysym.sym;
          if (keySym == SDLK_RETURN) {
            insertCharAtCursor(e, '\n');
          } else if (keySym == SDLK_TAB) { // ignore if editor just got focus
            // insertCharAtCursor(e, '\t');
          } else if (keySym == SDLK_s && event.key.keysym.mod & KMOD_CTRL) {
            saveFile(e);
          } else if (keySym == SDLK_DELETE) {
            deleteCharAtCursor(e);
          } else if (keySym == SDLK_LEFT && e->cursor > 0) {
            moveLeft(e);
          } else if (keySym == SDLK_RIGHT && e->cursor < e->textLen) {
            moveRight(e);
          }
          break;
        }
      }
      renderText(e);
    }
    SDL_Delay(1);
  }
}


int main(int argc, char **argv) {
  if (argc != 2) {
    die("Usage: e /path/to/file");
  }
  E e = init(argv[1]);
  if (!initUI(&e)) {
    goto error;
  }
  runEditor(&e);
  if (e.error) {
    goto error;
  }
  closeEditor(&e);
  return EXIT_SUCCESS;

  error:
  if (e.error) {
    printf("%s\n", e.error);
  }
  closeEditor(&e);
  return EXIT_FAILURE;
}