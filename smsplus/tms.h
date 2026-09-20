/*
    TMS9918A video modes for SG-1000 cartridges.
    Ported from SMS Plus GX tms.c (C) 1998-2007 Charles MacDonald, GPL v2.
*/

#ifndef _TMS_H_
#define _TMS_H_

/* Fixed TMS9918A palette, RGB888, indexed by the 4-bit colour code */
extern const uint8 tms_palette_rgb[16][3];

/* Draw one of the 192 active lines into linebuf (all 256 bytes), updating
   the 5S/C status */
void tms_render_line(int line);

/* Update the 5S/C status for one line of a skipped frame, drawing nothing.
   Safe to call for any line. */
void tms_line_collision(int line);

#endif /* _TMS_H_ */
