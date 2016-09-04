#ifndef TEXT_H
#define TEXT_H

/* ncurses frontend */

int txtinit(void);
void txtfree(void);
void txtgetdim(int *y, int *x);
void txthdr(void);
int txtmain(void);

#endif
