#include <pico8.h>

using namespace std;  
using namespace pico8;  

namespace {
  constexpr u8  FLAG_WALL = 1;
  constexpr u8  FLAG_SENSOR = 2;
  constexpr u8  SPR_EMPTY         = 0;
  constexpr u8  SPR_FLYER         = 4;
  constexpr u8  SPR_GROUND_GREEN  = 9;
  constexpr u8  SPR_GROUND        = 8;
  constexpr u8  SPR_PIPELINE      = 16;
  constexpr u8  SPR_TITLE         = 80;
  constexpr u8  SPR_SENSOR        = 10;
  constexpr u8  SPR_CLOUD         = 12;
  constexpr fx8 VJUMP(-29,10);
  constexpr fx8 GRAVITY(17,100);
  constexpr b8PpuBgTile BG_TILE_PIPE_L = {.YTILE=1, .XTILE=9, };
  constexpr b8PpuBgTile BG_TILE_PIPE_R = {.YTILE=1, .XTILE=10, };
  constexpr b8PpuBgTile BG_TILE_PIPE_L_VFLIP = {.YTILE=1, .XTILE=9, .VFP=1 };
  constexpr b8PpuBgTile BG_TILE_PIPE_R_VFLIP = {.YTILE=1, .XTILE=10,.VFP=1 };
  constexpr u8  PAL_COIN_BLINK = 3;
  constexpr u8  PAL_SHADOW     = 4;
  constexpr u8  YT_GROUND = 23;
  constexpr BgTiles XTILES = TILES_32;
  constexpr BgTiles YTILES = TILES_32;
  constexpr int DARK_SCORE = 900; 
  constexpr int CLEAR_SCORE = 1000; 
  constexpr u16 PRIV_KEY = 0xbfae; 
}

enum class  GameState { Nil, Title, Playing, Clear };

constexpr  inline u8  tileId(b8PpuBgTile tile ) {
  return static_cast<u8>((tile.YTILE << 4) | (tile.XTILE & 0x0F));
} 

class FlappyFlyerApp : public Pico8 {
  int frame = 0;
  GameState  reqReset = GameState::Nil;
  GameState  status   = GameState::Nil;
  Vec cam;
  Vec pos_flyer;
  Vec v_flyer;
  int xgen_map = 0;
  fx8 ygen;
  bool dead = false;
  bool req_red = false;
  u8 dcnt_stop_update = 0;
  int hi_score = 0;
  int score = 0;
  int disp_score = 0;
  int xlast_got_score = 0;
  int cnt_title = 0;
  int cnt_clear = 0;

  int calculatePipeSpan() {
    if (score <= 10) {
      return 8;
    } else if (score <= 20) {
      static constexpr int values[] = {8, 7, 7, 7, 6};
      return rndt(values);
    } else if (score <= 50) {
      static constexpr int values[] = {7, 6, 7, 7, 7};
      return rndt(values);
    } else if (score <= 75) {
      static constexpr int values[] = {8, 7, 6, 6, 7, 6, 6};
      return rndt(values);
    } else if (score <= 100) {
      static constexpr int values[] = {7, 7, 6, 6, 6, 6, 6};
      return rndt(values);
    } else if (score <= 110) {
      static constexpr int values[] = {9, 8, 8, 7};
      return rndt(values);
    } else if (score <= 140) {
      static constexpr int values[] = {7, 6, 6, 6, 6};
      return rndt(values);
    } else if (score <= 180) {
      static constexpr int values[] = {6, 6, 6, 6, 5};
      return rndt(values);
    } else {
      static constexpr int values[] = {6, 6, 5, 5, 5};
      return rndt(values);
    }
  }

  void  generateMapColumns(){
    int xdst = pos_flyer.x + 192;
    while( xgen_map < xdst ){
      int yy;
      const u32 xt = (xgen_map >> 3) & (XTILES-1);
      mset(xt,YT_GROUND,  SPR_GROUND_GREEN);
      for( yy=YT_GROUND+1 ; yy<YTILES ; ++yy ){
        mset(xt,yy,SPR_GROUND);
      }

      xgen_map += 8; 

      if( !(xt & 7) && xgen_map > 128 ){
        for( yy=0 ; yy<YT_GROUND ; ++yy ){
          mset(xt,  yy,SPR_EMPTY);
          mset(xt+1,yy,SPR_EMPTY);
        }

        const int yt = int(ygen)>>3;
        const int ytop = yt - 3;
        for( yy=0 ; yy<ytop ; ++yy ){
          mset(xt,  yy,SPR_PIPELINE);
          mset(xt+1,yy,SPR_PIPELINE+1);
        }
        msett(xt,  ytop,BG_TILE_PIPE_L_VFLIP); 
        msett(xt+1,ytop,BG_TILE_PIPE_R_VFLIP);

        const int ybottom = ytop + calculatePipeSpan();
        if( ybottom < YT_GROUND ){ 
          msett(xt,  ybottom,BG_TILE_PIPE_L); 
          msett(xt+1,ybottom,BG_TILE_PIPE_R);
          for( yy=ybottom+1 ; yy<YT_GROUND ; ++yy ){
            mset(xt,  yy,SPR_PIPELINE);
            mset(xt+1,yy,SPR_PIPELINE+1);
          }
        }

        // sensor
        for( yy=ytop+1 ; yy<=ybottom-1 ; ++yy ){
          mset(xt+1,yy,SPR_SENSOR);
        }

        ygen += pico8::rnd(96)-fx8(48);
        ygen = pico8::mid(ygen ,32, (YT_GROUND<<3)-48 );
      }
    }
  }

  u8  checkCollision() {
    return  fget(
      mget(
        (static_cast< u32 >( pos_flyer.x ) >> 3) & (XTILES-1),
        (static_cast< u32 >( pos_flyer.y ) >> 3) & (YTILES-1)
      ),
      0xff
    );
  }

  void _init() override {
    extern  const uint8_t  b8_image_sprite0[];
    hi_score = 53;
    lsp(0, b8_image_sprite0);
    mapsetup(XTILES, YTILES,std::nullopt,B8_PPU_BG_WRAP_REPEAT,B8_PPU_BG_WRAP_REPEAT);

    fset( tileId(BG_TILE_PIPE_L) ,      0xff, FLAG_WALL);
    fset( tileId(BG_TILE_PIPE_R) ,      0xff, FLAG_WALL);
    fset( tileId(BG_TILE_PIPE_L_VFLIP) ,0xff, FLAG_WALL);
    fset( tileId(BG_TILE_PIPE_R_VFLIP) ,0xff, FLAG_WALL);
    fset( SPR_GROUND,                   0xff, FLAG_WALL);
    fset( SPR_GROUND_GREEN,             0xff, FLAG_WALL);
    fset( SPR_PIPELINE  ,               0xff, FLAG_WALL);
    fset( SPR_PIPELINE+1,               0xff, FLAG_WALL);
    fset( SPR_SENSOR,0xff,FLAG_SENSOR);

    reqReset = GameState::Title;
  }

  void updatePlaying(){
    if( (!dead) && btnp( BUTTON_ANY ) ) v_flyer.y = VJUMP;

    pos_flyer += v_flyer;
    v_flyer.y = v_flyer.y + GRAVITY;

    cam.x = pos_flyer.x - 32;
    cam.y = 0;

    pos_flyer.y = pico8::max( pos_flyer.y , 0 );

    const u8 collide = checkCollision();
    if( !dead ){
      req_red = dead = (collide == FLAG_WALL);
      if( dead ){
        dcnt_stop_update = 7;
      }
    }

    if( (!dead) && pos_flyer.x > xlast_got_score + 9 ){
      if( collide == FLAG_SENSOR )  ++score;
      hi_score = pico8::max( score , hi_score);
      xlast_got_score = pos_flyer.x;
    }

    if( score >= CLEAR_SCORE ){
      reqReset = GameState::Clear;
    } else if( dead && pos_flyer.y > 240 ){
      reqReset = GameState::Title;
    }

    generateMapColumns();
  }

  void  enterTitle(){
    cnt_title = 0;
    print("\e[3;7H    ");
    print("\e[3q\e[13;4H HI:%d\e[0q" , hi_score );
    print("\e[15;4H SC:%d", score );
  }

  void  enterPlaying(){
    print("\e[2J");

    pos_flyer.set(0,64);
    v_flyer.set(fx8(2,2),0);
    xgen_map = pos_flyer.x - 64;
    ygen = pos_flyer.y;
    dead = false;
    score  = 0;
    disp_score = -1;
    xlast_got_score = 0;
    b8PpuBgTile tile = {};
    mcls(tile);
    generateMapColumns();
  }

  void  enterClear(){
    cnt_clear = 0;

    char pass_str[5];
    snprintf(pass_str, sizeof(pass_str), "%04X", PRIV_KEY); // 例: "A3F9"

    b8PpuBgTile tile = {};
    mcls(tile);
    int y = 5;

    print("\e[%d;0H----------------",y);
    ++y;
    print("\e[%d;0HCongratulations!",y);
    ++y;
    print("\e[%d;0H----------------",y);

    y+=3;
    print("\e[%d;0HYou win $100 USD",y);
    y+=2;
    print("\e[%d;0HClaim your prize:",y);
    y+=3;

    char line[32];
    snprintf(
      line, sizeof(line),
      "\e[%d;0HPass:\e[3q  %s\e[0q",y, pass_str
    );
    print(line);
    y+=3;

    print("\e[%d;0HTweet this:",y);
    ++y;

    print("\e[%d;0H@happy_homhom",y);
    y+= 2;

    print("\e[%d;0HWe'll contact",y);
    ++y;
    print("\e[%d;0Hyou via PayPal",y);
    ++y;

    print("\e[%d;0Hpayment.",y);
    ++y;
  }

  void  updateTitle() {
    generateMapColumns();
    cnt_title++;
    if( btnp( BUTTON_ANY ) ) reqReset = GameState::Playing;
  }

  void _update() override {
    ++frame;

    if( reqReset != GameState::Nil ){
      switch( reqReset ){
        case  GameState::Nil: break;
        case  GameState::Title:   enterTitle();   break;
        case  GameState::Playing: enterPlaying(); break;
        case  GameState::Clear:   enterClear();   break;
      }
      status = reqReset; 
      reqReset = GameState::Nil;
    }

    if( dcnt_stop_update > 0 ){
      --dcnt_stop_update;
      return;
    }

    switch( status ){
      case GameState::Playing:  updatePlaying();break;
      case GameState::Title:    updateTitle();  break;
      case GameState::Nil:                      break;
      case GameState::Clear:    ++cnt_clear;    break;
    }
  }

  void _draw() override {
    // Enable or disable the debug string output via dprint().
    dprintenable(false);
    pal( WHITE, RED , 3 );
    camera();

    if( status != GameState::Clear ){
      Color col = score < DARK_SCORE ? BLUE : DARK_BLUE;
      cls(req_red ? RED : col);
    } else {
      if( cnt_clear < 50 ){ 
        cls( static_cast< Color >( (cnt_clear>>2) & 15 ) );
      } else {
        cls( PINK );
      }
    }
    req_red = false;

    setz(maxz()-1);
    camera(cam.x, cam.y);
    map(cam.x, cam.y, BG_0);

    setz(maxz()-3);

    const u8 palsel = 1;
    pal(WHITE, BLACK, palsel);

    // Draw the yellow round-faced Foo sprite.
    switch( status ){
      case  GameState::Clear:{
        camera();
        setz(1);
        spr(SPR_TITLE,4,4,15,4);
        const u8 anm = ((cnt_clear>>2)&1)<<1;
        setz(maxz());
        spr(SPR_FLYER + anm, (cnt_clear<<1)&255, 140-24, 2, 2);
      }break;
      case  GameState::Nil:
      case  GameState::Title:{
        camera();
        setz(1);
        spr(SPR_TITLE,4, pico8::min(48,(cnt_title*3)-32),15,4);
        const u8 anm = ((cnt_title>>3)&1)<<1;
        spr(SPR_FLYER + anm, (cnt_title+44)&255, 140, 2, 2);
      }break;
      case  GameState::Playing:{
        const u8 anm = dead ? 0 : ((static_cast< u32 >( pos_flyer.y ) >> 3) & 1)<<1;
        spr(SPR_FLYER + anm, pos_flyer.x-8, pos_flyer.y-8, 2, 2, false, dead );

        if( score != disp_score ){
          disp_score = score; 
          print("\e[21;1H%d",disp_score);
        }
      }break;
    }

    camera();
    setz(maxz());
    spr(SPR_CLOUD, (((255-(frame>>2)))&255)-64,7,4,4);
  }
public: virtual ~FlappyFlyerApp(){}
};

int main() {
  FlappyFlyerApp  app;
  app.run();
  return 0;
}