
/**
 @file font.c

 @brief Font display for ili9341 driver
 Displays fonts generated by bdffont2c BDF to C code converter
 BDF = Glyph Bitmap Distribution Format
 The code handles fixed, proportional and bounding box format fonts
 @see http://en.wikipedia.org/wiki/Glyph_Bitmap_Distribution_Format

 @par Copyright &copy; 2015 Mike Gore, GPL License
 @par You are free to use this code under the terms of GPL
   please retain a copy of this notice in any code you use it in.

This is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option)
any later version.

This software is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <user_config.h>

/// @brief save fonts in flash
#define MEMSPACE_FONT ICACHE_FLASH_ATTR

/// @brief Include the Generated Font table
/// The generated tables always include Font specifications:
/// width, height, offsets, font type, etc
/// Font information: name, copyright, style information
/// Note: FONSPECS and FONTINFO defines controls actual usage.
/// We can also overide FONTSPECS here or in the Makefile for testing.
/// #define FONTSPECS
#include "fonts.h"

// All the fonts - defined in fonts.h
extern _font *allfonts[];

/// @brief  Get font attributes for a font
/// @param[in] *win: Window Structure
/// @param[in] c: character
/// @param[in] *f: font structure
/// @return  void
int font_attr(window *win, int c, _fontc *f)
{
    int offset;
    int num;
    unsigned char *ptr;

// check font
    _font *z = allfonts[win->font];
    _fontspecs s;

    num = c - z->First;
    if(num < 0 || num >= z->Glyphs)
        return(-1);

    f->ptr = z->bitmap;

// If we have font specifications defined and included we can use them.
// Notes: Normally for small fixed fonts we do not want to included them.
// However; if the font is large we can define just the active part of 
// the charater to reduce the overall size. 

    if(z->specs)
    {
        f->Width = z->Width;
        f->Height = z->Height;

// Copy the full font specification into ram for easy access
// This does not use much memory as it does not include the bitmap itself
// This method avoids memory agignment access errors on the ESP8266.

        cpy_flash((uint8_t *)&(z->specs[num]), (uint8_t *)&s,sizeof(_fontspecs));

		// Fonts Width,Height,X,Y
        f->w = s.Width;
        f->h = s.Height;
        f->x = s.X;
        f->y = s.Y;

		// Bitmap offset
        offset = s.Offset;
        f->ptr += offset;

	
		// The user may override the fixed variable font flag
        // This reduces the width of fixed fonts to just the active pixels.
        f->fixed = win->fixed;

		// Skip is the combined width, and an optional character spacing gap
		// Some characters have no active size (like the space character )
		// so we just use the master font width and gap
        if(f->fixed || !f->w)
            f->skip = z->Width + z->gap;
        else
			f->skip = f->x + f->w + z->gap;	// Include the X offset, Width and Gap
    }

    else   
    {
		// No Specifications, therefore the font must be fixed.
		// We create one using the master font size spec
		// There are no proportional options here.
        f->Width = z->Width;
        f->Height = z->Height;

        f->w = z->Width;
        f->h = z->Height;
        f->x = 0;
        f->y = 0;

		// FIXME - fixed fonts without specs have no proportional modes to use
        f->fixed = win->fixed;

		f->skip = z->Width + z->gap;

		// Each bitmap is a bit array w by h in size without any padding
		// except at the end of the array which is rounded to the next byte boundry.
        offset = ((z->Width * z->Height)+7)/8; /* round to byte boundry */
        f->ptr += (offset * num);

    }

	// FIXME
	// This zero size skip test should never be needed - for now we increase to 1..
	if(!f->skip)
		f->skip++;
// =====================================

#ifdef ILI9341_DEBUG
    ets_uart_printf("c: %02x font:%d w:%d h:%d x:%d y:%d gap:%d, W:%d, H:%d\r\n",
        0xff & c, 0xff & win->font, f->w, f->h, f->x, f->y, f->gap, f->Width, f->Height);
#endif
    return(win->font);
}


/// @brief Display a character and optionally wrap the graphic cursor
/// @see tft_putch  for main output function
/// Does not handle control characters
/// @param[in] *win: Window Structure of active window
/// @param[in] c: character
/// @return  void
/// TODO: To make proportional fonts render even better we should reduce
/// the font gap so that all of the active pixels between two fonts come no 
/// nearer then gap.
/// Example: consider the case of "Td", the 'd' can actually be part way INSIDE the 
/// T's area without touching.
/// We could keep a list of all left most active pixels of the previous character
/// and test then against all the right most active pixels of the current character 
/// to adjust the skip spacing value to a fixed gap.
///
void tft_drawChar(window *win, uint8_t c)
{
    _fontc f;
    int ret;
    int yskip;

    ret = font_attr(win, c, &f);
    if(ret < 0)
        return;

// process wrapping - will the character fit ?
	if(win->x + f.skip >= win->w)
	{
		if(win->wrap)
		{
			win->y += f.Height;
			win->x = 0;
		}
		else
		{
			return;
		}
	}
	if(win->y >= win->h)
	{
		if(win->wrap)
		{
			win->y = 0;
		}
		else
		{
			return;
		}
	}

// Conditionally clear the character area - not needed for full size fixed fonts..
// If the character is not full size then pre-clear the full font bit array
// (saves the more complex tests of clearing additional areas around the active font)
// Note: We use skip instead of Width because of the additiional gap that must also
// be cleared..

// FIXME we should do this in two parts - font area and then the gap area.
// Since we alwasy want to clear the gap - but not always the font - when fixed.

// Optionally clear the font area, then the gap
    if(f.h != f.Height ||  f.w != f.skip || f.x != 0 || f.y != 0)
        tft_fillRectWH(win, win->x, win->y, f.skip, f.Height, win->bg);

// This can happen for characters with no active pixels like the space character.
    if(!f.h || !f.w)
	{
		win->x += f.skip;
        return;
	}

// Top of bit bounding box ( first row with a 1 bit in it)
    yskip = f.Height - (f.y+f.h);

// Write the font to the screen
    tft_bit_blit(win, f.ptr, win->x+f.x, win->y+yskip, f.w, f.h, win->fg, win->bg);

	// skip is the offset to the next character
	win->x += f.skip;
}
