#include <pico8.h>

using namespace std;  
using namespace pico8;  

namespace {
  constexpr BgTiles XTILES = TILES_32;
  constexpr BgTiles YTILES = TILES_32;
  constexpr int DARK_SCORE = 900; 
  constexpr int CLEAR_SCORE = 1000; 
  constexpr u16 PRIV_KEY = 0x7219; 
  constexpr u16 ZFIFO = 512;

  constexpr int YPIX_TOP    = 50;
  constexpr int YPIX_BOTTOM = 150;
  constexpr int YLEN = YPIX_BOTTOM - YPIX_TOP;
  constexpr int W_NEAR = 110;
}

enum class  GameState { Nil, Title, Playing, Clear };

class RaceApp : public Pico8 {
  int frame = 0;
  GameState  reqReset = GameState::Nil;
  GameState  status   = GameState::Nil;
  Vec cam;
  bool dead = false;
  bool req_red = false;
  u8 dcnt_stop_update = 0;
  int hi_score = 0;
  int score = 0;
  int disp_score = 0;
  int cnt_title = 0;
  int cnt_playing = 0;
  int cnt_clear = 0;

  // playing 
  fx8 xCenter[  ZFIFO ];
  fx8 vxCenter[ ZFIFO ];

  void  enterPlaying(){
    print("\e[2J");

    fill(begin(xCenter),  end(xCenter), fx8(0));
    fill(begin(vxCenter), end(vxCenter),fx8(0));

    cnt_playing = 0;

    dead = false;
    score  = 0;
    disp_score = -1;
    b8PpuBgTile tile = {};
    mcls(tile);
  }

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

  void _init() override {
    extern  const uint8_t  b8_image_sprite0[];
    hi_score = 53;
    lsp(0, b8_image_sprite0);
    mapsetup(XTILES, YTILES,std::nullopt,B8_PPU_BG_WRAP_REPEAT,B8_PPU_BG_WRAP_REPEAT);
    reqReset = GameState::Title;
  }

  void updatePlaying(){
    if( score >= CLEAR_SCORE ){
      reqReset = GameState::Clear;
    } else if( dead ){
      reqReset = GameState::Title;
    }
  }

  void  enterTitle(){
    cnt_title = 0;
    print("\e[3;7H    ");
    print("\e[3q\e[13;4H HI:%d\e[0q" , hi_score );
    print("\e[15;4H SC:%d", score );
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
      cls( DARK_BLUE );
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
      }break;
      case  GameState::Nil:
      case  GameState::Title:{
        camera();
        setz(1);
      }break;
      case  GameState::Playing:{
        ++cnt_playing;

        if( score != disp_score ){
          disp_score = score; 
          print("\e[21;1H%d",disp_score);
        }

        setz(1);
        line(0,YPIX_BOTTOM,128,YPIX_BOTTOM,WHITE);
        fx8 x_center = fx8(64);
        fx8 vx_center = 0;
        fx8 ax_center = fx8(21,100) * pico8::sin( fx8(cnt_playing,100) );
        const int yspan = 2;
        fx8 width = W_NEAR;

        for( int y=YPIX_BOTTOM ; y>YPIX_TOP ; y -= yspan ){
          fx8 ox_center = x_center;
          vx_center += ax_center;
          x_center  += vx_center;
          line(ox_center,y,x_center,y - yspan,DARK_GREY);

          line(ox_center-width,y,x_center-width,y - yspan,DARK_GREY);
          line(ox_center+width,y,x_center+width,y - yspan,DARK_GREY);
          width -= fx8(213,100);
        }    

      }break;
    }

    camera();
    setz(maxz());
    //spr(SPR_CLOUD, (((255-(frame>>2)))&255)-64,7,4,4);
  }
public: virtual ~RaceApp(){}
};

int main() {
  RaceApp  app;
  app.run();
  return 0;
}