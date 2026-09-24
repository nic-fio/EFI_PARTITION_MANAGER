/* partmgr: drawing on the text console and the dialogs (messages, questions,
 * text fields, menus). Colours are the EFI palette. */
#ifndef PARTMGR_UI_H
#define PARTMGR_UI_H

#include "../pal/pal.h"

enum { BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LIGHTGRAY, DARKGRAY, LIGHTBLUE, LIGHTGREEN, LIGHTCYAN,
       LIGHTRED, LIGHTMAGENTA, YELLOW, WHITE };

extern int ui_cols, ui_rows;

void ui_init(void); /* reads the console size */
void ui_text(int col, int row, int width, int fg, int bg, const char *text);
void ui_textf(int col, int row, int width, int fg, int bg, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
void ui_title(const char *left, const char *right);
void ui_keys(int line, const char *keys); /* line 0: the last row, 1: the one above */

/* Key bars at the bottom of the screen: every key in a blue box with its
 * label after it, always in the same place and the same colours. With two
 * bars (the second may be NULL) both use the same style: boxes with a space
 * on each side and the group names, when that fits the screen; else narrow
 * boxes; else narrow boxes without the group names. BAR1 is the upper one. */
typedef struct {
    const char *key, *label;
} UiKey;
typedef struct {
    const char *group;
    const UiKey *keys;
    int n;
} UiKeyBar;
void ui_keybars(const UiKeyBar *bar1, const UiKeyBar *bar2);
void ui_clear_body(void);

PalKey ui_key(void);
bool ui_is_enter(const PalKey *k);
bool ui_is_esc(const PalKey *k);

/* Dialogs, drawn over the screen; the caller redraws it afterwards.
 * warn: white on red, for anything that destroys data. When ui_dialogs is
 * set (the graphical window, gui.c), the four functions below are its
 * dialogs instead of the text ones; they behave the same. */
typedef struct {
    void (*message)(bool warn, const char *title, const char *text);
    bool (*yesno)(bool warn, const char *title, const char *text);
    bool (*input)(bool warn, const char *title, const char *text, char *buf, size_t n);
    int (*menu)(const char *title, const char *const *items, int n, int sel);
} UiDialogs;
extern const UiDialogs *ui_dialogs;

void ui_message(bool warn, const char *title, const char *text); /* waits for a key */
bool ui_yesno(bool warn, const char *title, const char *text);  /* y / n, Esc = no; TEXT ends with (Y/N) */
/* A text field prefilled with BUF (a key other than an editing one replaces
 * the text). Returns false on Esc. */
bool ui_input(bool warn, const char *title, const char *text, char *buf, size_t n);
/* A list of ITEMS; returns the index chosen or -1 on Esc. */
int ui_menu(const char *title, const char *const *items, int n, int sel);

#endif
