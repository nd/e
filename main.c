#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <SDL.h>
#include <assert.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define MIN(a, b) (a < b) ? (a) : (b)

int die(char *msg) {
  if (errno) {
    perror(msg);
  } else {
    printf("%s\n", msg);
  }
  exit(EXIT_FAILURE);
}


typedef struct E_Glyph {
  SDL_Texture *texture;
  int h;
  int w;
  int bearingX;
  int bearingY;
  int advance;
  bool initialized;
} E_Glyph;

typedef struct E {
  const char *path;
  const char *fileName;
  char *text;
  size_t textLen;
  char lineBuf[1000];
  const char *error;
  bool quit;
  size_t cursor;
  int height;
  int width;
  int textHeight;
  int statusLineHeight;
  int statusLineBaselineOffset;

  int lineHeight;
  int visibleLineCursor;
  int visibleLineCount;
  int visibleLineTop;

  int columnWidth;
  int columnCursor;
  int columnCount;
  int columnLeft;

  SDL_Window *window;
  SDL_Renderer *renderer;

  FT_Library ftLib;
  FT_Face ftFace;
  E_Glyph glyphs[256];
  FT_Pos kerning[256 * 256];

  Uint64 perfCountFreqMS;
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

  // file name
  int i = strlen(path) - 1;
  for (; i >= 0; i--) {
    if (path[i] == '/') {
      i++;
      break;
    }
  }
  size_t fileNameLen = strlen(path) - i;
  char *fileName = xalloc(fileNameLen + 1);
  strncpy(fileName, &path[i], fileNameLen);
  fileName[fileNameLen] = '\0';

  fclose(file);

  FT_Library ftLib;
  FT_Error error = FT_Init_FreeType(&ftLib);
  if (error) {
    die("Failed to init ft");
  }

  return (E) {
          .path = path,
          .fileName = fileName,
          .height=768,
          .width=1024,
          .text = text,
          .textLen = strlen(text),
          .ftLib = ftLib,
          .perfCountFreqMS = SDL_GetPerformanceFrequency() / 1000,
  };
}


void initVisibleLines(E *e) {
  e->lineHeight = e->ftFace->size->metrics.height >> 6;
  e->statusLineBaselineOffset = abs(e->ftFace->size->metrics.descender >> 6);
  e->statusLineHeight = e->lineHeight + e->statusLineBaselineOffset;
  e->textHeight = e->height - e->statusLineHeight;
  e->visibleLineCount = floor((e->textHeight - e->statusLineHeight) * 1.0 / e->lineHeight);

  E_Glyph glyph = e->glyphs['A'];
  e->columnWidth = glyph.advance;
  e->columnCount = floor(e->width * 1.0 / e->columnWidth);
}


void setKerning(E *e, unsigned char left, unsigned char right, int kerning) {
  e->kerning[left * 256 + right] = kerning;
}

int getKerning(E *e, unsigned char left, unsigned char right) {
  return e->kerning[left * 256 + right];
}

E_Glyph *getGlyph(E *e, unsigned char c) {
  return e->glyphs[c].initialized ? &e->glyphs[c] : &e->glyphs['?'];
}

bool initFont(E *e) {
  FT_Face face;
  FT_Error error = FT_New_Face(e->ftLib, "/home/nd/Downloads/JetBrainsMono-Regular.ttf", 0, &face);
  if (error) {
    e->error = "Failed to init face";
    return false;
  }
  e->ftFace = face;
  int fontSize = 12;
  error = FT_Set_Char_Size(face, 0, fontSize*64, 96, 96);
  if (error) {
    e->error = "Failed to init font size";
    return false;
  }

  Uint8 alpha_table[256];
  for (int i = 0; i < SDL_arraysize(alpha_table); ++i) {
    alpha_table[i] = (Uint8)i;
  }

  for (int c = 0; c < 255; c++) {
    if (isprint(c)) {
      error = FT_Load_Char(face, c, FT_LOAD_RENDER);
      if (error) {
        continue;
      }
      FT_GlyphSlot glyph = face->glyph;
      FT_Bitmap bitmap = glyph->bitmap;
      SDL_Texture *texture = 0;
      if (bitmap.rows) {
        SDL_Surface *surface = SDL_CreateRGBSurface(0, bitmap.width, bitmap.rows, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        for (int i = 0; i < bitmap.rows; i++) {
          int srcRowStart = i * bitmap.pitch;
          Uint32 *dst = (Uint32 *)surface->pixels + i * surface->pitch / 4;
          for (int j = 0; j < bitmap.width; j++) {
            unsigned char gray = bitmap.buffer[srcRowStart + j];
            Uint8 alpha = alpha_table[gray];
            Uint32 pixel = ((Uint32) alpha << 24);
            *dst++ = pixel;
          }
        }
        texture = SDL_CreateTextureFromSurface(e->renderer, surface);
        SDL_FreeSurface(surface);
      }

      e->glyphs[c] = (E_Glyph){
              .texture = texture,
              .h = bitmap.rows,
              .w = bitmap.width,
              .bearingX = glyph->metrics.horiBearingX >> 6,
              .bearingY = glyph->metrics.horiBearingY >> 6,
              .advance = glyph->metrics.horiAdvance >> 6,
              .initialized = true,
      };
    }
  }
  E_Glyph *tab = &e->glyphs['\t'];
  tab->advance = e->glyphs[' '].advance * 2;
  tab->initialized = true;
  if (FT_HAS_KERNING(face)) {
    for (int left = 0; left < 255; left++) {
      if (isprint(left)) {
        FT_UInt leftIndex = FT_Get_Char_Index(face, left);
        for (int right = 0; right < 255; right++) {
          if (isprint(right)) {
            FT_UInt rightIndex = FT_Get_Char_Index(face, right);
            FT_Vector kerning = {0};
            FT_Get_Kerning(face, leftIndex, rightIndex, FT_KERNING_DEFAULT, &kerning);
            setKerning(e, left, right, kerning.x >> 6);
          }
        }
      }
    }
  }
  return true;
}


bool initUI(E *e) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    setEditorError(e, SDL_GetError());
    return false;
  }
  e->window = SDL_CreateWindow(e->path, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, e->width, e->height,
          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!e->window) {
    setEditorError(e, SDL_GetError());
    return false;
  }
  e->renderer = SDL_CreateRenderer(e->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC );
  if (!e->renderer) {
    setEditorError(e, SDL_GetError());
    return false;
  }
  if (!initFont(e)) {
    return false;
  }
  initVisibleLines(e);
  return true;
}


void closeEditor(E *e) {
  if (e->text) {
    free(e->text);
  }
  if (e->ftLib) {
    FT_Done_FreeType(e->ftLib);
  }
  if (e->renderer) {
    SDL_DestroyRenderer(e->renderer);
  }
  if (e->window) {
    SDL_DestroyWindow(e->window);
  }
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

void fillCurrentLineAndOffset(E *e, int *lineIndex, int *lineStart) {
  LineIter iter = {.text = e->text, .textLen = e->textLen};
  int index = 0;
  while (lineIterNext(&iter)) {
    if (iter.lineStart <= e->cursor && e->cursor <= iter.lineStart + iter.lineLen) {
      *lineIndex = index;
      *lineStart = iter.lineStart;
      break;
    }
    index++;
  }
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

void renderCursor(E *e, int penX, int penY) {
  Uint8 r = 0, g = 0, b = 0, a = 0;
  SDL_GetRenderDrawColor(e->renderer, &r, &g, &b, &a);
  SDL_SetRenderDrawColor(e->renderer, 0x0, 0x0, 0xff, 0xf);
  SDL_Rect cursorRect = {penX, penY - e->lineHeight, 2, e->lineHeight + 5};
  SDL_RenderFillRect(e->renderer, &cursorRect);
  SDL_SetRenderDrawColor(e->renderer, r, g, b, a);
}

void renderGlyph(E *e, E_Glyph *glyph, int penX, int penY, bool drawGlyphBox) {
  if (glyph && glyph->texture) {
    if (drawGlyphBox) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      SDL_GetRenderDrawColor(e->renderer, &r, &g, &b, &a);
      SDL_SetRenderDrawColor(e->renderer, 0xff, 0x0, 0x0, 0xf);
      SDL_RenderDrawLine(e->renderer, penX + glyph->bearingX, penY - glyph->bearingY, penX + + glyph->bearingX + glyph->w, penY - glyph->bearingY);
      SDL_RenderDrawLine(e->renderer, penX + glyph->bearingX, penY - glyph->bearingY + glyph->h, penX + glyph->bearingX + glyph->w, penY - glyph->bearingY + glyph->h);
      SDL_RenderDrawLine(e->renderer, penX + glyph->bearingX, penY - glyph->bearingY, penX + glyph->bearingX, penY - glyph->bearingY + glyph->h);
      SDL_RenderDrawLine(e->renderer, penX + glyph->bearingX + glyph->w, penY - glyph->bearingY, penX + glyph->bearingX + glyph->w, penY - glyph->bearingY + glyph->h);
      SDL_SetRenderDrawColor(e->renderer, r, g, b, a);
    }
    SDL_Rect dstRect = (SDL_Rect){penX + glyph->bearingX, penY - glyph->bearingY, glyph->w, glyph->h};
    SDL_RenderCopy(e->renderer, glyph->texture, 0, &dstRect);
  }
}

void renderLine(E *e, char *line, size_t size, int penX, int penY) {
  char prev = 0;
  for (int i = 0; i < size; i++) {
    char c = line[i];
    E_Glyph *glyph = getGlyph(e, c);
    renderGlyph(e, glyph, penX, penY, false);
    penX += glyph->advance;
    if (prev) {
      penX += getKerning(e, prev, c);
    }
    prev = c;
  }
}

void debugRender(E *e) {
  SDL_SetRenderDrawColor(e->renderer, 0xff, 0xff, 0xff, 0xff);
  SDL_RenderClear(e->renderer);

  int penx = 300, peny = 400;

  SDL_SetRenderDrawColor(e->renderer, 0x0, 0x0, 0xff, 0xff);
  SDL_RenderDrawLine(e->renderer, penx, peny-50, penx, peny+50);
  SDL_RenderDrawLine(e->renderer, penx - 50, peny, penx + 50, peny);

  SDL_SetRenderDrawColor(e->renderer, 0x0, 0x0, 0x0, 0xff);
  char *txt = "public static void Main() {}";
  char prev = 0;
  for (int i = 0; i < strlen(txt); i++) {
    char c = txt[i];
    E_Glyph *glyph = getGlyph(e, c);
    renderGlyph(e, glyph, penx, peny, false);
    penx += glyph->advance;
    if (prev) {
      penx += getKerning(e, prev, c);
    }
    prev = c;
  }
  SDL_RenderPresent(e->renderer);
}

void renderText(E *e) {
  SDL_SetRenderDrawColor(e->renderer, 0xff, 0xff, 0xff, 0xff);
  SDL_RenderClear(e->renderer);
  int currentLine = getCurrentLineIndex(e);
  int firstLine = e->visibleLineTop;
  LineIter iter = (LineIter){.text = e->text, .textLen = e->textLen};
  int penY = e->lineHeight;
  int lineNum = 0;
  char prev = 0;
  int winHeight = e->textHeight;
  int winWidth = e->width;
  while (lineIterNext(&iter)) {
    if (lineNum < firstLine) {
      lineNum++;
      continue;
    }

    int lineLen = iter.lineLen;
    if (lineLen < e->columnLeft) {
      penY += e->lineHeight;
      lineNum++;
      continue;
    }

    int lineStart = iter.lineStart + e->columnLeft;
    int lineEnd = iter.lineStart + iter.lineLen;
    int penX = 0;
    for (int i = lineStart; i < lineEnd; i++) {
      if (penX > winWidth) {
        break;
      }
      if (lineNum == currentLine && i == e->cursor) {
        renderCursor(e, penX, penY);
      }
      char c = e->text[i];
      E_Glyph *glyph = getGlyph(e, c);
      renderGlyph(e, glyph, penX, penY, false);
      penX += glyph->advance;
      if (prev) {
        penX += getKerning(e, prev, c);
      }
      prev = c;
    }
    // space in the end of line to be able to continue it
    if (penX < winWidth) {
      if (lineNum == currentLine && lineEnd == e->cursor) {
        renderCursor(e, penX, penY);
      }
      renderGlyph(e, getGlyph(e, ' '), penX, penY, false);
    }

    if (penY > winHeight) {
      break;
    }
    penY += e->lineHeight;
    lineNum++;
  }
}

void renderStatusLine(E *e, Uint64 t0) {
  SDL_SetRenderDrawColor(e->renderer, 0xdc, 0xdc, 0xdc, 0xff);
  SDL_Rect statusLineRect = {0, e->height - e->statusLineHeight, e->width, e->statusLineHeight};
  SDL_RenderFillRect(e->renderer, &statusLineRect);
  SDL_SetRenderDrawColor(e->renderer, 0x0, 0x0, 0x0, 0xff);
  SDL_RenderDrawLine(e->renderer, 0, e->height - e->statusLineHeight, e->width, e->height - e->statusLineHeight);
  Uint64 t1 = SDL_GetPerformanceCounter();
  double duration = (t1 - t0) * 1.0 / e->perfCountFreqMS;
  if (duration > 1000) {
    duration = 1000;
  }

  int lineIndex = 0;
  int lineStart = 0;
  fillCurrentLineAndOffset(e, &lineIndex, &lineStart);
  int count = snprintf(e->lineBuf, 1000, "  %s (%d:%d)   %.1fms", e->fileName, lineIndex+1, e->cursor - lineStart, duration);
  renderLine(e, e->lineBuf, count, 0, e->height - e->statusLineBaselineOffset);
}

void updateUI(E *e) {
  Uint64 t0 = SDL_GetPerformanceCounter();
  renderText(e);
  renderStatusLine(e, t0);
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

void handleResize(E *e, int w, int h) {
  e->width = w;
  e->height = h;
  initVisibleLines(e);
}

void runEditor(E *e) {
  updateUI(e);
  SDL_Event event;
  while (!e->quit) {
    int eventCount = 0;
    SDL_StartTextInput();
    while (SDL_PollEvent(&event)) {
      eventCount++;
      bool render = false;
      switch (event.type) {
        case SDL_QUIT:
          e->quit = true;
          break;
        case SDL_TEXTINPUT: {
          size_t textLen = strlen(event.text.text);
          for (size_t i = 0; i < textLen; i++) {
            insertCharAtCursor(e, event.text.text[i]);
          }
          render = true;
          break;
        }
        case SDL_KEYDOWN: {
          SDL_Keycode keySym = event.key.keysym.sym;
          if (keySym == SDLK_RETURN) {
            insertCharAtCursor(e, '\n');
          } else if (keySym == SDLK_TAB) { // ignore if editor just got focus
            // insertCharAtCursor(e, '\t');
          } else if (event.key.keysym.mod & KMOD_CTRL) {
            switch (keySym) {
              case SDLK_s:
                saveFile(e);
                break;
              case SDLK_r:
                render = false;
                debugRender(e);
                break;
              case SDLK_e:
                render = true;
                break;
            }
          } else if (keySym == SDLK_s && event.key.keysym.mod & KMOD_CTRL) {
            saveFile(e);
            render = true;
          } else if (keySym == SDLK_DELETE) {
            deleteCharAtCursor(e);
            render = true;
          } else if (keySym == SDLK_LEFT && e->cursor > 0) {
            moveLeft(e);
            render = true;
          } else if (keySym == SDLK_RIGHT && e->cursor < e->textLen) {
            moveRight(e);
            render = true;
          }
          break;
        }
        case SDL_WINDOWEVENT: {
          if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            handleResize(e, event.window.data1, event.window.data2);
          }
          render = true;
          break;
        }
      }
      if (render) {
        updateUI(e);
      }
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