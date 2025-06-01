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

static  constexpr u8  FLAG_WALL = 1;

static  constexpr u8  SPR_FLYER         = 4;
static  constexpr u8  SPR_GROUND_GREEN  = 9;
static  constexpr u8  SPR_GROUND        = 8;

static  constexpr u8  SPR_ICE       = 10;
static  constexpr u8  SPR_PIPE      = 25;
static  constexpr u8  SPR_PIPELINE  = 81;

static  constexpr b8PpuBgTile BG_TILE_PIPE_L = {.YTILE=1, .XTILE=9, };
static  constexpr b8PpuBgTile BG_TILE_PIPE_R = {.YTILE=1, .XTILE=10, };
static  constexpr b8PpuBgTile BG_TILE_PIPE_L_VFLIP = {.YTILE=1, .XTILE=9, .VFP=1 };
static  constexpr b8PpuBgTile BG_TILE_PIPE_R_VFLIP = {.YTILE=1, .XTILE=10,.VFP=1 };

static  u8  getv(b8PpuBgTile tile ){
  return  (tile.YTILE<<4) + tile.XTILE;
} 

static  constexpr u8  PAL_COIN_BLINK = 3;
static  constexpr u8  PAL_SHADOW     = 4;

static  constexpr u8  YT_GROUND = 23;

static  int frame = 0;

static  constexpr array<unsigned char, 16> palette_shadow = {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

extern  const uint8_t  b8_image_sprite0[];

static  Vec pos_flyer;
static  Vec v_flyer;
static  int xgen_map;
static  int ygen;

static  void  blinkCoin();

static  constexpr BgTiles XTILES = TILES_32;
static  constexpr BgTiles YTILES = TILES_32;

// Equivalent to PICO-8's _init()
static  void  init() {
  lsp(0, b8_image_sprite0);
  mapsetup(XTILES, YTILES,std::nullopt,B8_PPU_BG_WRAP_REPEAT,B8_PPU_BG_WRAP_REPEAT);

  // Set flags for each sprite pattern (BG patterns)
 //fset(SPR_WALL0, FLAG_WALL, 1);
 //fset(SPR_WALL1, FLAG_WALL, 1);
 //fset(SPR_WALL2, FLAG_WALL, 1);
 //fset(SPR_COIN,  FLAG_COIN, 1);
 //for (u8 ii = SPR_FLOWER; ii < 48; ++ii) fset(ii, FLAG_FRUITE, 1);

  fset( getv(BG_TILE_PIPE_L) ,        FLAG_WALL, 1);
  fset( getv(BG_TILE_PIPE_R) ,        FLAG_WALL, 1);
  fset( getv(BG_TILE_PIPE_L_VFLIP) ,  FLAG_WALL, 1);
  fset( getv(BG_TILE_PIPE_R_VFLIP) ,  FLAG_WALL, 1);
  fset( SPR_GROUND,FLAG_WALL, 1);
  fset( SPR_GROUND_GREEN ,FLAG_WALL, 1);
  fset( SPR_PIPELINE  ,FLAG_WALL,1);
  fset( SPR_PIPELINE+1,FLAG_WALL,1);

  reqReset = RESET_GAME;;
}

static  void  genMap(){
  int xdst = pos_flyer.x + 192;
  while( xgen_map < xdst ){
    const u32 xt = (xgen_map >> 3) & (XTILES-1);
    mset(xt,YT_GROUND,  SPR_GROUND_GREEN);
    mset(xt,YT_GROUND+1,SPR_GROUND);
    xgen_map += 8; 

    if( !(xt & 7) ){
      int yy;
      const int yt = ygen>>3;
      const int ytop = yt - 3;
      for( yy=0 ; yy<ytop ; ++yy ){
        mset(xt,  yy,SPR_PIPELINE);
        mset(xt+1,yy,SPR_PIPELINE+1);
      }
      msett(xt,  ytop,BG_TILE_PIPE_L_VFLIP); 
      msett(xt+1,ytop,BG_TILE_PIPE_R_VFLIP);

      const int ybottom = yt + 3;
      msett(xt,  ybottom,BG_TILE_PIPE_L); 
      msett(xt+1,ybottom,BG_TILE_PIPE_R);
      for( yy=ybottom+1 ; yy<YT_GROUND ; ++yy ){
        mset(xt,  yy,SPR_PIPELINE);
        mset(xt+1,yy,SPR_PIPELINE+1);
      }
    }
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
        v_flyer.set( fx8(1,2) , 0 );
        xgen_map = pos_flyer.x - 64;
        ygen = pos_flyer.y;
        b8PpuBgTile tile = {};
        mcls(tile);
        genMap();
      }break;
    }
    reqReset = RESET_NIL;
  }

  if( btnp( BUTTON_ANY ) ){
    v_flyer.y = -fx8(2);
  }

  pos_flyer += v_flyer;
  v_flyer.y += fx8(10,100);

  cam.x = pos_flyer.x - 32;
  cam.y = 0;

  const u32 ux = (static_cast< u32 >( pos_flyer.x ) >> 3) & (XTILES-1);
  const u32 uy = (static_cast< u32 >( pos_flyer.y ) >> 3) & (YTILES-1);
  //printf( "ux,uy=%ld,%ld\n",ux,uy);

  const u16 spridx = mget(ux,uy);
  if( fget(spridx,FLAG_WALL) ){
    printf( "COLLIDE\n" );
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
  void _init() override { init(); }
  void _update() override { update(); }
  void _draw() override { draw(); }
public: virtual ~Pico8App(){}
};
int main() {
  Pico8App app;
  app.run();
  return 0;
}