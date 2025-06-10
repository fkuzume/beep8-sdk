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

  constexpr fx12 YPIX_TOP    = 50;
  constexpr fx12 YPIX_BOTTOM = 150;
  //constexpr fx12 YLEN = YPIX_BOTTOM - YPIX_TOP;
  constexpr fx12 W_NEAR = 110;

  constexpr u16 N_FIFO_MAPDATA = 32;  // must be 2^n

  static  Xorshift32 xors;

  inline fx8 to_fx8(fx12 v){ return static_cast<fx8>(v); }

  inline  void line_fx12(fx12 x0, fx12 y0, fx12 x1, fx12 y1, Color color) {
    const fx8 ix0 = to_fx8(x0);
    const fx8 iy0 = to_fx8(y0);
    const fx8 ix1 = to_fx8(x1);
    const fx8 iy1 = to_fx8(y1);
    line(ix0, iy0, ix1, iy1, color);
  }

  fx12 rndf12(fx12 x0, fx12 x1){
    if (x0 > x1) {
      const fx12 temp(x0);
      x0 = x1;
      x1 = temp;
    }

    const int32_t min_raw = x0.raw_value();
    const int32_t max_raw = x1.raw_value();
    const int32_t random_raw = xors.next_range(min_raw, max_raw);

    fx12 result;
    result.set_raw_value(random_raw);
    return result;
  }
}

enum class  GameState { Nil, Title, Playing, Clear };
struct  MapData {
  fx12 distance;
  fx12 ax;       // fx12(±21,100)
};

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

  fx12    xCar;
  fx12    xCam;
  fx12    distance;
  u16     upMapData;
  MapData mapData[ N_FIFO_MAPDATA ];

  // playing 
  void  enterPlaying(){
    print("\e[2J");

    xCam = xCar = 0;
    distance = 0;

    upMapData = 1;
    for( u16 nn=0 ; nn < N_FIFO_MAPDATA ; ++nn ){
      MapData& md = mapData[ nn ];
      md.distance = rndf12( 30,100);
      if( nn <= 2 ){
        md.ax = 0;
      } else {
        md.ax = rndf12( fx12(-21,100), fx12(+21,100) );
      }
    }

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

    distance += fx12(1); // TODO:velocity
    MapData& md = mapData[ upMapData ];
    if( distance > md.distance ){
      distance -= md.distance;
      upMapData = (upMapData + 1) & (N_FIFO_MAPDATA-1);
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

        setz(maxz());
        line_fx12(0,YPIX_BOTTOM,128,YPIX_BOTTOM,WHITE);
        fx12 x_center = fx12(64);
        fx12 vx_center = 0;
        //fx12 ax_center = fx12(21,100) * pico8::sin( fx12(cnt_playing,100) );

        const u16 idx_0 = (upMapData - 1)  & (N_FIFO_MAPDATA - 1);
        const MapData& md_0 = mapData[ idx_0 ];

        const u16 idx_1 = upMapData & (N_FIFO_MAPDATA - 1);
        const MapData& md_1 = mapData[ idx_1 ];

        const fx12 t = distance / md_1.distance;
        const fx12 ax_center = (fx12(1)-t) * md_0.ax + t * md_1.ax;

        const fx12 yspan = 2;
        fx12 width = W_NEAR;
        fx12 ox_center;

        for( fx12 y=YPIX_BOTTOM ; y>YPIX_TOP ; y -= yspan ){
          ox_center = x_center;
          vx_center += ax_center;
          x_center  += vx_center;
          line_fx12(ox_center,y,x_center,y - yspan,DARK_GREY);
          line_fx12(ox_center-width,y,x_center-width,y - yspan,DARK_GREY);
          line_fx12(ox_center+width,y,x_center+width,y - yspan,DARK_GREY);
          width -= fx12(213,100);
        }    

        // mycar
        setz(3);


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