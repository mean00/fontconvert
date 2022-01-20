/*
TrueType to Adafruit_GFX font converter.  Derived from Peter Jakobs'
Adafruit_ftGFX fork & makefont tool, and Paul Kourany's Adafruit_mfGFX.

NOT AN ARDUINO SKETCH.  This is a command-line tool for preprocessing
fonts to be used with the Adafruit_GFX Arduino library.

For UNIX-like systems.  Outputs to stdout; redirect to header file, e.g.:
  ./fontconvert ~/Library/Fonts/FreeSans.ttf 18 > FreeSans18pt7b.h

REQUIRES FREETYPE LIBRARY.  www.freetype.org

Currently this only extracts the printable 7-bit ASCII chars of a font.
Will eventually extend with some int'l chars a la ftGFX, not there yet.
Keep 7-bit fonts around as an option in that case, more compact.

See notes at end for glyph nomenclature & other tidbits.
*/

#include <ctype.h>
#include <ft2build.h>
#include <stdint.h>
#include <stdio.h>
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_TRUETYPE_DRIVER_H
#include "gfxfont.h" // Adafruit_GFX font structures
#include "string"
#include "regex"
#include "vector"

#define DPI 141 // Approximate res. of Adafruit 2.8" TFT
/**
 * 
 */
class BitPusher
{
public:
    BitPusher()
    {
        acc=0;
        bit=7;
        cur=buffer;
    }
    const uint8_t *data() {return buffer;}
    void addBit(bool onoff)
    {
        if(onoff) acc|=(1<<bit);
        bit--;
        if(bit<0)
        {
            align();
        }
    }
    void    align()
    {
        if(bit==7) return;
        *cur=acc;
        acc=0;
        cur++;
        bit=7;
    }
    int  offset()
    {
        return (int)(cur-buffer);
    }
    int    bit;    
    int    acc;
    uint8_t *cur;
    uint8_t buffer[256*1024];
};

/**
 * 
 * @param fontFile
 * @param symbolName
 */
class FontConverter
{
public:
                        FontConverter(const std::string &fontFile, const std::string &symbolName);
                        ~FontConverter();
        bool           init(int size,int first, int last);
        bool           convert();
        void           printIndex();
        void           printFooter();
        void           printBitmap();
        
protected:
    bool                initFreeType(int size);
   
    FT_Library          library;
    FT_Face             face;
    std::string         fontFile,symbolName;
    bool                ftInited;
    int                 first,last;  
    std::vector<GFXglyph > listOfGlyphs;
    BitPusher           bitPusher;
    int                 face_height;
    
};

/**
 * 
 * @param xfontFile
 * @param xsymbolName
 */
 FontConverter::FontConverter(const std::string &xfontFile, const std::string &xsymbolName)
 {
    fontFile=xfontFile;
    symbolName=xsymbolName;
    ftInited=false;    
    face_height=0;
 }
 FontConverter::~FontConverter()
 {
    if(ftInited)
        FT_Done_FreeType(library);
 }
 /**
  * 
  * @return 
  */
 bool FontConverter::convert()
 {
     int err;
     FT_Glyph glyph;
     for(int i=first;i<=last;i++)
     {
        GFXglyph thisGlyph= (GFXglyph){0,0,0,0,0,0};
    // MONO renderer provides clean image with perfect crop
    // (no wasted pixels) via bitmap struct.
    bool renderingOk=true;
    if ((err = FT_Load_Char(face, i, FT_LOAD_TARGET_MONO))) {     fprintf(stderr, "Error %d loading char '%c'\n", err, i); renderingOk=false;   }
    if ((err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO))) {      fprintf(stderr, "Error %d rendering char '%c'\n", err, i);     renderingOk=false;  }
    if ((err = FT_Get_Glyph(face->glyph, &glyph))) {      fprintf(stderr, "Error %d getting glyph '%c'\n", err, i);    renderingOk=false;    }

    if(!renderingOk)
    {
        listOfGlyphs.push_back(thisGlyph);  
        continue;
    }
    FT_Bitmap *bitmap = &face->glyph->bitmap;
    FT_BitmapGlyphRec *g= (FT_BitmapGlyphRec *)glyph;

    // Minimal font and per-glyph information is stored to
    // reduce flash space requirements.  Glyph bitmaps are
    // fully bit-packed; no per-scanline pad, though end of
    // each character may be padded to next byte boundary
    // when needed.  16-bit offset means 64K max for bitmaps,
    // code currently doesn't check for overflow.  (Doesn't
    // check that size & offsets are within bounds either for
    // that matter...please convert fonts responsibly.)
    bitPusher.align();
    thisGlyph.bitmapOffset = bitPusher.offset();
    thisGlyph.width = bitmap->width;
    thisGlyph.height = bitmap->rows;
    thisGlyph.xAdvance = face->glyph->advance.x >> 6;
    thisGlyph.xOffset = g->left;
    thisGlyph.yOffset = 1 - g->top;
    listOfGlyphs.push_back(thisGlyph);
    
    for (int y = 0; y < bitmap->rows; y++) 
    {
      for (int x = 0; x < bitmap->width; x++) 
      {
        int byte = x / 8;
        int bit = 0x80 >> (x & 7);
        bitPusher.addBit(bitmap->buffer[y * bitmap->pitch + byte] & bit);
      }
    }
    
    }
     
    face_height= face->size->metrics.height >> 6;
    FT_Done_Glyph(glyph);  
    return true;
 }
 
 /**
  * 
  * @return 
  */
bool    FontConverter::initFreeType(int size)
{
  int err;   
  // Init FreeType lib, load font
  if ((err = FT_Init_FreeType(&library))) 
  {
    fprintf(stderr, "FreeType init error: %d", err);
    return false;
  }
  
  // Use TrueType engine version 35, without subpixel rendering.
  // This improves clarity of fonts since this library does not
  // support rendering multiple levels of gray in a glyph.
  // See https://github.com/adafruit/Adafruit-GFX-Library/issues/103
  FT_UInt interpreter_version = TT_INTERPRETER_VERSION_35;
  FT_Property_Set(library, "truetype", "interpreter-version", &interpreter_version);
  // prepare names
  ftInited=true;
  if ((err = FT_New_Face(library, fontFile.c_str(), 0, &face))) 
  {
    fprintf(stderr, "Font load error: %d", err);
    FT_Done_FreeType(library);
    return err;
  }

  // << 6 because '26dot6' fixed-point format
  FT_Set_Char_Size(face, size << 6, 0, DPI, 0);
  return true;
}
/**
 * 
 * @param size
 * @return 
 */
  bool    FontConverter::init(int size, int xfirst, int xlast)
  {
      if(!initFreeType(size)) return false;
      if(xlast>xfirst)          
      {
        first=xfirst;
        last=xlast;
      }else
      {
         first=xlast;
         last=xfirst ; 
      }           
      return true;
  }
  /**
   * 
   */
void   FontConverter::printIndex()
{
  printf("const GFXlyph %sGlyphs[] PROGMEM = {\n", symbolName.c_str());
  for(int i = first;i <= last; i++) 
  {
    GFXglyph &glyph=listOfGlyphs[i-first];
    printf("  { %5d, %3d, %3d, %3d, %4d, %4d }", glyph.bitmapOffset,
           glyph.width, glyph.height, glyph.xAdvance, glyph.xOffset,
           glyph.yOffset);
    
    int c='X';
    if ((i >= ' ') && (i <= '~')) c=i;
    printf(",   // 0x%02X %c \n", i,c);
  }
}
/**
 * 
 */
void FontConverter::printBitmap()
{
    
  printf("const uint8_t %sBitmaps[] PROGMEM = {\n ", symbolName.c_str());
  bitPusher.align();
  int sz=bitPusher.offset();
  const uint8_t *data=bitPusher.data();
  
  int tab=0;
  for(int i=0;i<sz;i++)
  {
      printf(" 0x%02X,",data[i]);
      tab++;
      if(tab==12)
      {
          printf("\n ");
          tab=0;
      }
  }

  printf(" };\n\n"); // End bitmap array

}

/**
 * 
 */
void   FontConverter::printFooter()
{
  
  // Output font structure
  printf("const GFXfont %s PROGMEM = {\n", symbolName.c_str());
  printf("  (uint8_t  *)%sBitmaps,\n", symbolName.c_str());
  printf("  (GFXglyph *)%sGlyphs,\n", symbolName.c_str());
  if(!face_height) 
  {  // No face height info, assume fixed width and get from a glyph.  
    printf("  0x%02X, 0x%02X, %d };\n\n", first, last, listOfGlyphs[0].height);
  } 
  else 
  {
    printf("  0x%02X, 0x%02X, %d };\n\n", first, last, face_height);
  }
    
  int sz=bitPusher.offset();
  printf("// Bitmap : about %d bytes (%d kBytes)\n",sz,(sz+1023)/1024);
  sz=(last-first+1)*sizeof(GFXglyph);
  printf("// Header : about %d bytes (%d kBytes)\n",sz,(sz+1023)/1024);
  sz+=bitPusher.offset()+sizeof(GFXfont);
  printf("//--------------------------------------\n");
  printf("// total : about %d bytes (%d kBytes)\n",sz,(sz+1023)/1024);
}

/**
 * 
 */
void usage()
{
    fprintf(stderr, "Usage:  fontfile size [first] [last]\n");
    exit(1);
}

/**
 * 
 * @param argc
 * @param argv
 * @return 
 */
int main(int argc, char *argv[]) 
{
 
  if (argc < 3) 
  {
    usage();    
    return 1;
  }

  int size = atoi(argv[2]);
  int first,last;

  first=' ';
  last='~';
  switch(argc)
  {
      case 3: break;
      case 4:
            last = atoi(argv[3]);
            break;
      case 5:
            first = atoi(argv[3]);
            last = atoi(argv[4]);
            break;
      default:
          usage();          
  }
 
  
  std::string fontFile=std::string(argv[1]);  
  std::string fileName = fontFile.substr(fontFile.find_last_of("/\\") + 1);
  fileName= std::regex_replace(fileName, std::regex(" "), "_");  
  std::string::size_type const p(fileName.find_last_of('.'));
  fileName = fileName.substr(0, p);
  
  printf("Processing font %s\n",fontFile.c_str());
  printf("Generating symbol %s\n",fileName.c_str());
  
  char ext[16];
  sprintf(ext, "%dpt%db", size, (last > 127) ? 8 : 7);
  
  // full var name
  std::string symbolName=fileName+std::string(ext);
  
  FontConverter *converter=new FontConverter(fontFile,symbolName);
  
  if(!converter->init(size,first,last))
  {
      printf("Failed to init converter\n");
      exit(1);
  }
  if(!converter->convert())
  {
      printf("Failed to convert\n");
      exit(1);
  }
  converter->printBitmap();
  converter->printIndex();
  converter->printFooter();
  
  delete converter;
  converter=NULL;  

  return 0;
}

/* -------------------------------------------------------------------------

Character metrics are slightly different from classic GFX & ftGFX.
In classic GFX: cursor position is the upper-left pixel of each 5x7
character; lower extent of most glyphs (except those w/descenders)
is +6 pixels in Y direction.
W/new GFX fonts: cursor position is on baseline, where baseline is
'inclusive' (containing the bottom-most row of pixels in most symbols,
except those with descenders; ftGFX is one pixel lower).

Cursor Y will be moved automatically when switching between classic
and new fonts.  If you switch fonts, any print() calls will continue
along the same baseline.

                    ...........#####.. -- yOffset
                    ..........######..
                    ..........######..
                    .........#######..
                    ........#########.
   * = Cursor pos.  ........#########.
                    .......##########.
                    ......#####..####.
                    ......#####..####.
       *.#..        .....#####...####.
       .#.#.        ....##############
       #...#        ...###############
       #...#        ...###############
       #####        ..#####......#####
       #...#        .#####.......#####
====== #...# ====== #*###.........#### ======= Baseline
                    || xOffset

glyph->xOffset and yOffset are pixel offsets, in GFX coordinate space
(+Y is down), from the cursor position to the top-left pixel of the
glyph bitmap.  i.e. yOffset is typically negative, xOffset is typically
zero but a few glyphs will have other values (even negative xOffsets
sometimes, totally normal).  glyph->xAdvance is the distance to move
the cursor on the X axis after drawing the corresponding symbol.

There's also some changes with regard to 'background' color and new GFX
fonts (classic fonts unchanged).  See Adafruit_GFX.cpp for explanation.
*/

