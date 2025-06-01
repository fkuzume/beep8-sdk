#include <pico8.h>

using namespace std;  
using namespace pico8;  

enum ReqReset { RESET_NIL, RESET_TITLE, RESET_GAME };

static  constexpr u8  FLAG_WALL = 1;
static  constexpr u8  SPR_FLYER         = 4;
static  constexpr u8  SPR_GROUND_GREEN  = 9;
static  constexpr u8  SPR_GROUND        = 8;
static  constexpr u8  SPR_PIPELINE      = 16;
static  constexpr u8  SPR_TITLE         = 80;

static  constexpr fx8 VJUMP(-3);
static  constexpr fx8 GRAVITY(17,100);

static  constexpr b8PpuBgTile BG_TILE_PIPE_L = {.YTILE=1, .XTILE=9, };
static  constexpr b8PpuBgTile BG_TILE_PIPE_R = {.YTILE=1, .XTILE=10, };
static  constexpr b8PpuBgTile BG_TILE_PIPE_L_VFLIP = {.YTILE=1, .XTILE=9, .VFP=1 };
static  constexpr b8PpuBgTile BG_TILE_PIPE_R_VFLIP = {.YTILE=1, .XTILE=10,.VFP=1 };


static  constexpr u8  PAL_COIN_BLINK = 3;
static  constexpr u8  PAL_SHADOW     = 4;
static  constexpr u8  YT_GROUND = 23;
static  constexpr BgTiles XTILES = TILES_32;
static  constexpr BgTiles YTILES = TILES_32;
static  constexpr array<unsigned char, 16> palette_shadow = {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

// work ram
static  int frame = 0;
static  ReqReset  reqReset = RESET_NIL;
static  ReqReset  status = RESET_NIL;

// game work ram
static  Vec cam;
static  Vec pos_flyer;
static  Vec v_flyer;
static  int xgen_map;
static  int ygen;
static  bool dead;
static  bool req_red = false;
static  u8 dcnt_stop_update = 0;
static  int hi_score;
static  int score;
static  int disp_score;
static  int cnt_title;

static  u8  getv(b8PpuBgTile tile ) {
  return  (tile.YTILE<<4) + tile.XTILE;
} 

static  void  init() {
  extern  const uint8_t  b8_image_sprite0[];
  hi_score = 50;
  lsp(0, b8_image_sprite0);
  mapsetup(XTILES, YTILES,std::nullopt,B8_PPU_BG_WRAP_REPEAT,B8_PPU_BG_WRAP_REPEAT);


  fset( getv(BG_TILE_PIPE_L) ,        FLAG_WALL, 1);
  fset( getv(BG_TILE_PIPE_R) ,        FLAG_WALL, 1);
  fset( getv(BG_TILE_PIPE_L_VFLIP) ,  FLAG_WALL, 1);
  fset( getv(BG_TILE_PIPE_R_VFLIP) ,  FLAG_WALL, 1);
  fset( SPR_GROUND,FLAG_WALL, 1);
  fset( SPR_GROUND_GREEN ,FLAG_WALL, 1);
  fset( SPR_PIPELINE  ,FLAG_WALL,1);
  fset( SPR_PIPELINE+1,FLAG_WALL,1);

  reqReset = RESET_GAME;;
  reqReset = RESET_TITLE;;
}

static  void  genMap(){
  int xdst = pos_flyer.x + 192;
  while( xgen_map < xdst ){
    const u32 xt = (xgen_map >> 3) & (XTILES-1);
    mset(xt,YT_GROUND,  SPR_GROUND_GREEN);
    mset(xt,YT_GROUND+1,SPR_GROUND);
    xgen_map += 8; 

    if( !(xt & 7) && xgen_map > 128 ){
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

static  bool  chkIfCollide() {
  return  fget(
    mget(
      (static_cast< u32 >( pos_flyer.x ) >> 3) & (XTILES-1),
      (static_cast< u32 >( pos_flyer.y ) >> 3) & (YTILES-1)
    ),
    FLAG_WALL
  );
}

static  void  update() {
  ++frame;

  if( reqReset != RESET_NIL ){
    switch( reqReset ){
      case  RESET_NIL:break;
      case  RESET_TITLE:{
        cnt_title = 0;
        print("\e[3;7H    ");
        print("\e[3q\e[13;4H HI:%d\e[0q" , hi_score );
        print("\e[15;4H SC:%d", score );
      }break;
      case  RESET_GAME:{
        pos_flyer.set(0,64);
        v_flyer.set(fx8(2,2),0);
        xgen_map = pos_flyer.x - 64;
        ygen = pos_flyer.y;
        dead = false;
        score  = 0;
        disp_score = -1;
        b8PpuBgTile tile = {};
        mcls(tile);
        genMap();
      }break;
    }
    status = reqReset; 
    reqReset = RESET_NIL;
  }

  if( dcnt_stop_update > 0 ){
    --dcnt_stop_update;
    return;
  }

  switch( status ){
    case RESET_GAME:{
      if( (!dead) && btnp( BUTTON_ANY ) ) v_flyer.y = VJUMP;

      pos_flyer += v_flyer;
      v_flyer.y = v_flyer.y + GRAVITY;

      cam.x = pos_flyer.x - 32;
      cam.y = 0;

      pos_flyer.y = pico8::max( pos_flyer.y , 0 );


      if( !dead ){
        req_red = dead = chkIfCollide();
        if( dead ){
          dcnt_stop_update = 7;
        }
      }

      if( dead && pos_flyer.y > 240 ) reqReset = RESET_TITLE;
    }break;
    case RESET_NIL:break;

    case RESET_TITLE:{
      cnt_title+=3;
      if( btnp( BUTTON_ANY ) ) reqReset = RESET_GAME;
    }break;
  }
}

static  void  draw() {
  // Enable or disable the debug string output via dprint().
  dprintenable(false);

  pal( WHITE, RED , 3 );

  // Initialize the camera state.
  camera();

  // Clear the entire screen with GREEN.
  cls(req_red ? RED : BLUE);
  req_red = false;

  // Applies depth setting to all subsequent draw calls:
  // 0 is frontmost, maxz() is backmost.
  setz(maxz());

  camera(cam.x, cam.y);
  genMap();

  map(cam.x, cam.y, BG_0);

  // Set depth to the foreground.
  setz(maxz()/2);

  // Set the palette.
  const u8 palsel = 1;
  pal(WHITE, BLACK, palsel);

  // Draw the yellow round-faced Foo sprite.
  switch( status ){
    case  RESET_NIL:
    case  RESET_TITLE:{
      camera();
      setz(1);
      spr(SPR_TITLE,4, pico8::min(48,cnt_title-32),15,4);
    }break;
    case  RESET_GAME:{
      const u8 anm = dead ? 0 : ((static_cast< u32 >( pos_flyer.y ) >> 3) & 1)<<1;
      spr(SPR_FLYER + anm, pos_flyer.x-8, pos_flyer.y-8, 2, 2, false, dead );

      if( score != disp_score ){
        disp_score = score; 
        print("\e[3;7H%d",disp_score);
      }
    }break;
  }
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