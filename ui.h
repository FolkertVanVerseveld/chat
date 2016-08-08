#ifndef UI_H
#define UI_H

void uiperror(const char *str);
void uistatus(const char *str);
void uistatusf(const char *format, ...) __attribute__((__format__(__printf__, 1, 2)));
int uimain(void);

#endif
