#ifndef DANYA_EDITOR_H
#define DANYA_EDITOR_H

#include "../include/types.h"

#define EDITOR_MAX_LINES 256
#define EDITOR_LINE_LEN  80
#define EDITOR_NAME_LEN  64

typedef struct {
    char lines[EDITOR_MAX_LINES][EDITOR_LINE_LEN];
    int  line_count;
    int  cursor_x;
    int  cursor_y;
    int  scroll_y;
    char filename[EDITOR_NAME_LEN];
    int  modified;
    int  running;
} editor_t;

void editor_open(const char* filename);
void editor_new(void);
void editor_run(void);

#endif
