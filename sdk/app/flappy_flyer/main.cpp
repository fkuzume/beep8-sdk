/*
  Simple sample of PICO-8 LikeLibrary for C/C++.

  A yellow round-faced character (Foo) walks randomly.
  You can also operate it with the PC keyboard arrow keys.
  On smartphones, you cannot operate it because there is no keyboard.
*/
#include <pico8.h>

using namespace std;  
using namespace pico8;  

enum ReqReset {
  RESET_NIL,
  /* --- */
  RESET_TITLE,
  RESET_GAME,
};
static  ReqReset  reqReset = RESET_NIL;

static  Vec cam;

static  constexpr u8  SPR_FLYER       = 4;

static  constexpr u8  PAL_COIN_BLINK = 3;
static  constexpr u8  PAL_SHADOW     = 4;

static  int frame = 0;

static  constexpr array<unsigned char, 16> palette_shadow = {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

extern  const uint8_t  b8_image_sprite0[];

// Foo's position
static  Vec pos_flyer;
static  int xgen_map;

// Foo's velocity
static  Vec v_flyer;

static  void  blinkCoin();

static  constexpr BgTiles XTILES = TILES_32;

// Equivalent to PICO-8's _init()
static  void  init() {
  lsp(0, b8_image_sprite0);
  mapsetup(XTILES, TILES_32,std::nullopt,B8_PPU_BG_WRAP_REPEAT,B8_PPU_BG_WRAP_REPEAT);

  // Set flags for each sprite pattern (BG patterns)
 //fset(SPR_WALL0, FLAG_WALL, 1);
 //fset(SPR_WALL1, FLAG_WALL, 1);
 //fset(SPR_WALL2, FLAG_WALL, 1);
 //fset(SPR_COIN,  FLAG_COIN, 1);
 //for (u8 ii = SPR_FLOWER; ii < 48; ++ii) fset(ii, FLAG_FRUITE, 1);

  reqReset = RESET_GAME;;
}

static  void  genMap(){
  int xdst = pos_flyer.x + 192;
  while( xgen_map < xdst ){
    printf( "xgen_map=%d\n", xgen_map );

    const u32 xt = (xgen_map >> 3) & (XTILES-1);

mset(xt,23,9);
mset(xt,24,8);


    xgen_map += 8; 
  }
}

static  void  update() {
  ++frame;

  if( reqReset != RESET_NIL ){
    switch( reqReset ){
      case  RESET_NIL:break;
      case  RESET_TITLE:{
      }break;
      case  RESET_GAME:{
        pos_flyer.set(0,64);
        xgen_map = pos_flyer.x - 64;
        b8PpuBgTile tile = {};
        tile.XTILE = 0;
        tile.YTILE = 2;
        mcls(tile);
        genMap();
      }break;
    }
    reqReset = RESET_NIL;
  }

  if( btnp( BUTTON_ANY ) ){
    printf( "anybtn\n");
  }
}

static  void  draw() {
  // Enable or disable the debug string output via dprint().
  dprintenable(false);

  // Initialize the camera state.
  camera();

  // Clear the entire screen with GREEN.
  cls(BLUE);

  // Applies depth setting to all subsequent draw calls:
  // 0 is frontmost, maxz() is backmost.
  setz(maxz());

  // Palette animation to make the coin sparkle.
  blinkCoin();

  pos_flyer.x += fx8(1,2);

  cam.x = pos_flyer.x - 32;
  cam.y = 0;
  camera(cam.x, cam.y);
  genMap();

  map(cam.x, cam.y, BG_0);

  // Set depth to the foreground.
  setz(maxz()/2);

  // Set the palette.
  const u8 palsel = 1;
  pal(WHITE, BLACK, palsel);

  // Draw the yellow round-faced Foo sprite.
  spr(SPR_FLYER, pos_flyer.x-8, pos_flyer.y-8, 2, 2 );
}

// Palette animation data for coin blinking
constexpr size_t PaletteCount = 3;
constexpr size_t PaletteSize  = 16;
const array<array<unsigned char, PaletteSize>, PaletteCount> palettes = {{
  {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
  {0,1,2,3,4,5,6,10,8,4,9,11,12,13,14,15},
  {0,1,2,3,4,5,6,9,8,9,4,11,12,13,14,15},
}};
static const u8 idx_coin[8] = {0,0,0,0,1,2,2,1};
static void blinkCoin() {
  const u8 idx = idx_coin[(frame>>2)&7];
  const auto& pal = palettes[idx];
  setpal(PAL_COIN_BLINK, pal);
}

class Pico8App : public Pico8 {
  // Initialization function called only once at startup.
  void _init() override {
    // In this sample we call init() for a C-style implementation,
    // but implementing directly in this member function _init()
    // would be more C++-style.
    init();
  }

  // Called every 1/60th of a second.
  // Only internal status updates are allowed.
  // Drawing operations are not allowed here.
  void _update() override {
    update();
  }

  // Called every 1/60th of a second.
  // Perform drawing of internal status here.
  void _draw() override {
    draw();
  }

public:
  virtual ~Pico8App(){}
};

// main() for C/C++ language.
// Magic incantation to run the PICO-8 library.
int main() {
  Pico8App app;
  app.run();  // ::run() enters an infinite loop.
  return 0;
}