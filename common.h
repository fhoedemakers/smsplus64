
#ifndef mytypes_TYPES_H_INCLUDED
#define mytypes_TYPES_H_INCLUDED

/*-------------------------------------------------------------------*/
/*  Type definition                                                  */
/*-------------------------------------------------------------------*/
#ifndef DWORD
typedef unsigned long  DWORD;
#endif /* !DWORD */

#ifndef WORD
typedef unsigned short WORD;
#endif /* !WORD */

#ifndef BYTE
typedef unsigned char  BYTE;
#endif /* !BYTE */

/*-------------------------------------------------------------------*/
/*  NULL definition                                                  */
/*-------------------------------------------------------------------*/
#ifndef NULL
#define NULL  0
#endif /* !NULL */

#define RGB888_TO_RGB5551(r, g, b) (((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1)
#define CBLACK (RGB888_TO_RGB5551(0, 0, 0))
#define CWHITE (RGB888_TO_RGB5551(255, 255, 255))
#define CRED (RGB888_TO_RGB5551(255, 0, 0))
#define CGREEN (RGB888_TO_RGB5551(76, 255, 0))
#define CBLUE (RGB888_TO_RGB5551(0, 38, 255))
#define CLIGHTBLUE (RGB888_TO_RGB5551(0, 148, 255))
#endif /* !nytypes_TYPES_H_INCLUDED */
