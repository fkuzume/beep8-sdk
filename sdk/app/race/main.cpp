#include <pico8.h>
#include "tbl_stepper.h"

using namespace std;  
using namespace pico8;  
namespace {
  constexpr BgTiles XTILES = TILES_32;
  constexpr BgTiles YTILES = TILES_32;
  constexpr int CLEAR_SCORE = 1000; 
  constexpr u16 PRIV_KEY = 0x9217; 
  constexpr size_t NOBJ = 16;
  constexpr int YSPAN = 4;

  constexpr int SPR_MYCACR_TAIL         = 48;
  constexpr int SPR_MYCACR_FRONT_WHEEL  = 32;
  constexpr int SPR_MYCACR_REAR_WHEEL   = 16;

  constexpr fx12 YPIX_TOP    = 70;
  constexpr fx12 YPIX_BOTTOM = 150;
  constexpr fx12 W_NEAR = 200;

  constexpr fx12 VZ_ENABLE_WHEEL = fx12(1);

  constexpr fx12 EVERY_50   = 50;
  constexpr fx12 EVERY_100  = 100;

  constexpr u16 N_FIFO_MAPDATA = 32;  // must be 2^n

  constexpr fx12 HW_CAR = 20; 
  constexpr fx12 X_SCREEN_OFFSET = 64;
  constexpr fx12 MAX_VZ = fx12(8);

  static  Xorshift32 xors;

  static  constexpr auto  flushAnimUsual  = to_array<pico8::Color>({BLACK});
  static  constexpr auto  flushAnimBurnt   = to_array<pico8::Color>({RED,BLACK,ORANGE,RED,BLACK,RED,BLACK,ORANGE,ORANGE,ORANGE,DARK_BLUE,ORANGE,RED,ORANGE,ORANGE,RED,BLACK});

  inline fx8 to_fx8(fx12 v){ return static_cast<fx8>(v); }

  inline  void line_fx12(fx12 x0, fx12 y0, fx12 x1, fx12 y1, Color color,fx12 sx=0) {
    const fx8 ix0 = to_fx8(x0+sx);
    const fx8 iy0 = to_fx8(y0);
    const fx8 ix1 = to_fx8(x1+sx);
    const fx8 iy1 = to_fx8(y1);
    line(ix0, iy0, ix1, iy1, color);
  }

  inline  void rectfill_fx12(fx12 x0, fx12 y0, fx12 x1, fx12 y1, Color color,fx12 sx=0) {
    const fx8 ix0 = to_fx8(x0+sx);
    const fx8 iy0 = to_fx8(y0);
    const fx8 ix1 = to_fx8(x1+sx);
    const fx8 iy1 = to_fx8(y1);
    rectfill(ix0, iy0, ix1, iy1, color);
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

  // https://www.geogebra.org/graphing?lang=ja
  constexpr int NTBL = 400;
  static  s16 tblz2y[ NTBL ];

  void  genTableZ2Y(){
    constexpr fx12  v100(100);
    constexpr fx12  v10000(10000);
    for( int z=0 ; z<NTBL ; ++z ){
      tblz2y[ z ] = static_cast< s16 >( YPIX_BOTTOM - ( v100 - v10000/(z+v100) ) );
    }
  }

  s16   z2y( fx12 z ){
    int iz = static_cast< int >( z );
    if( iz < 0 )      iz = 0;
    if( iz > NTBL-1 ) iz = NTBL-1;
    return tblz2y[ iz ];
  }

} // local namespace


struct  Point {
  fx12 x;
  fx12 y;
  Point(fx12 x_,fx12 y_ )
    : x(x_), y(y_)
  {}
  Point(){}
};

inline  void line_fx12( const Point& p0, const Point& p1, Color color, fx12 sx=0 ){
  const Vec pos0(
    to_fx8(p0.x + sx),
    to_fx8(p0.y)
  );
  const Vec pos1(
    to_fx8(p1.x + sx),
    to_fx8(p1.y)
  );
  line( Line( pos0, pos1 ) , color );
} 

enum class  GameState { Nil, Title, Playing, Clear };

struct  MapData {
  fx12 distance;
  fx12 ax;       // fx12(±21,100)
};

struct  Obj {
  size_t idx;
  enum State : uint8_t {
    Disappear,
    Appear
  };
  enum DrawType : uint8_t {
    Nothing,
    Car
  };
  State state       = Disappear;
  DrawType drawType = Nothing;

  fx12 x = 0;
  fx12 z = 0;
  fx12 vz = 0;
  fx12 hw = 35;  // half width
  bool isDrawed = false;
  bool  chkIfCollide();
  void  update();
  void  draw(fx12 t,fx12 x_center,fx12 wc,fx12 y);
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
public:
  int cnt_crash = 0;
  int cnt_clear = 0;

  TableStepper< pico8::Color > flushBg;
  fx12    xCar;
  int     xWheel;
  fx12    vzCar;
private:
  fx12    xCam;
  fx12    distance;
  fx12    acc_distance;
  fx12    every_50_distance;
  fx12    every_100_distance;
  u16     upMapData;
  MapData mapData[ N_FIFO_MAPDATA ];
  vector< Obj > objs = std::vector< Obj >( NOBJ );

  optional<size_t> allocObj(){
    for (size_t i = 0; i < objs.size(); ++i) {
      if (objs[i].state != Obj::Disappear) continue;

      Obj obj;
      obj.idx = i;
      obj.state = Obj::Appear;
      objs[i] = obj;
      return i;
    }
    return std::nullopt;
  }

  // playing 
  void  enterPlaying(){
    print("\e[2J");
    objs = std::vector< Obj >( NOBJ );

    flushBg.setTable( flushAnimUsual );

    xCam = xCar = vzCar = 0;
    xWheel = 0;
    acc_distance = distance = 0;
    every_50_distance = 0;
    every_100_distance = 0;

    upMapData = 1;
    for( u16 nn=0 ; nn < N_FIFO_MAPDATA ; ++nn ){
      MapData& md = mapData[ nn ];
      md.distance = rndf12(130,500);
      if( nn <= 2 ){
        md.ax = 0;
      } else {
        md.ax = rndf12( fx12(-19,100), fx12(+19,100) );
      }
    }

    cnt_playing = 0;
    dead = false;
    score  = 0;
    cnt_crash = 0;
    disp_score = -1;
    b8PpuBgTile tile = {};
    mcls(tile);
  }

  void _init() override {
    genTableZ2Y();
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

    if( cnt_crash > 0 ){
      cnt_crash++;
      WATCH( cnt_crash );
    }

    if( cnt_crash == 0 ){
      fx12 vxCar =0;
      if( btn( BUTTON_LEFT ) ){
        vxCar = -8;
        --xWheel;
      } else if( btn( BUTTON_RIGHT ) ){
        vxCar = +8;
        ++xWheel;
      } else {
        if( xWheel < 0 ){
          ++xWheel;
        } else if ( xWheel > 0 ){
          --xWheel;
        }
      }
      xWheel = std::clamp(xWheel,-6,+6);

      if( vzCar > VZ_ENABLE_WHEEL ){
        xCar += vxCar;
      } else {
        xWheel = 0;
      }

      if( btn( BUTTON_O ) ){
        vzCar -= fx12(200,4096);
      } else if ( btn( BUTTON_X ) ){
        vzCar += fx12(130,4096);
      } else {
        vzCar -= fx12(23,4096);
      }
    } else {
      vzCar *= fx12(3937,4096);
    }


    vzCar = std::clamp(vzCar, fx12(0), MAX_VZ );

    distance           += vzCar;
    acc_distance       += vzCar;
    every_50_distance  += vzCar;
    every_100_distance += vzCar;

    if( every_50_distance > EVERY_50 ){
      every_50_distance -= EVERY_50;
    }

    if( every_100_distance > EVERY_100 ){
      every_100_distance -= EVERY_100;

      auto idobj = allocObj();
      if( idobj ){
        Obj& obj = objs[ idobj.value() ];
        obj.x = -40;
        obj.z = +300;
        //obj.vz = fx12(3000,4096);
        obj.vz = 0;
      }
    }

    MapData& md = mapData[ upMapData ];
    if( distance > md.distance ){
      distance -= md.distance;
      upMapData = (upMapData + 1) & (N_FIFO_MAPDATA-1);
    }

    for( auto& obj : objs ){
      obj.update();
    }
  }

  void  enterTitle(){
    cnt_title = 0;
    print("\e[3;7H    ");
    print("\e[3q\e[13;4H HI:%d\e[0q" , hi_score );
    print("\e[15;4H SC:%d", score );
    flushBg.setTable( flushAnimUsual );
  }

  void  enterClear(){
    cnt_clear = 0;
    cnt_crash = 0;

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

  void  drawMyCar(){
    if( cnt_crash == 0 ){
      #if 0
      rectfill_fx12(
        xCar - HW_CAR - xCam,
        YPIX_BOTTOM - 12, 

        xCar + HW_CAR - xCam,
        YPIX_BOTTOM - 4,

        LIGHT_PEACH,
        X_SCREEN_OFFSET
      );
      #endif


      auto xx = to_fx8( xCar - xCam + 64 - 16 );
      auto yy = to_fx8(YPIX_BOTTOM - 12);

      setz(1);

      const int lxWheel  = xWheel>>1;
      const int lxWheel2 = xWheel>>2;
      const u32 uacc_distance = static_cast< u32 >( acc_distance );
      const u32 anm   = (uacc_distance>>4) & 1;
      const u32 yoff  = (uacc_distance>>5) & 1;
      u32 yoff_bd  = 0;
      if( ((uacc_distance+77) & 0xff) == 0 ){
        ++yoff_bd;
      }

      spr(
        SPR_MYCACR_FRONT_WHEEL+anm,
        xx+2+lxWheel,yy-4
      );
      spr(
        SPR_MYCACR_FRONT_WHEEL+anm,
        xx+24-2+lxWheel,yy-4
      );

      spr(
        SPR_MYCACR_TAIL,
        xx,yy+yoff_bd,
        2,1
      );
      spr(
        SPR_MYCACR_TAIL,
        xx+16,yy+yoff_bd,
        2,1,
        true
      );

      spr(
        SPR_MYCACR_REAR_WHEEL+anm,
        xx-lxWheel2,yy+3-yoff
      );
      spr(
        SPR_MYCACR_REAR_WHEEL+anm,
        xx+24-lxWheel2,yy+3-yoff
      );
    } else {
    }
  }

  void _draw() override {
    // Enable or disable the debug string output via dprint().
    dprintenable(false);
    pal( WHITE, RED , 3 );
    camera();

    cls( flushBg.step() );

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
        xCam = xCar;

        fx12 x_center = 0;
        fx12 vx_center = 0;
        //fx12 ax_center = fx12(21,100) * pico8::sin( fx12(cnt_playing,100) );

        const u16 idx_0 = (upMapData - 1)  & (N_FIFO_MAPDATA - 1);
        const MapData& md_0 = mapData[ idx_0 ];

        const u16 idx_1 = upMapData & (N_FIFO_MAPDATA - 1);
        const MapData& md_1 = mapData[ idx_1 ];

        const fx12 t = distance / md_1.distance;
        const fx12 ax_center = (fx12(1)-t) * md_0.ax + t * md_1.ax;
        //const fx12 ax_center = 0;

        const fx12 yspan( YSPAN );
        fx12 ox_center;

        const fx12 YRANGE = YPIX_BOTTOM - YPIX_TOP;

        for( auto& obj : objs ){
          if( obj.state == Obj::Disappear ) continue;
          obj.isDrawed = false; 
        }

        Point pcenter;
        Point pleft;
        Point pright;
        Point center;
        Point left;
        Point right;

        int nn = 0;
        for( fx12 y=YPIX_BOTTOM ; y>YPIX_TOP ; y -= yspan , ++nn ){
          ox_center = x_center;

          vx_center += ax_center;
          x_center  += vx_center;
          vx_center += ax_center;
          x_center  += vx_center;

          const fx12 tt     = (y - YPIX_TOP ) / YRANGE;  // TODO:
          const fx12 width  = W_NEAR * tt;
          const fx12 wc     = -xCam  * tt;

          center.x = wc + ox_center;
          center.y = y;

          left.x  = center.x - width;
          left.y  = center.y;

          right.x = center.x + width;
          right.y = center.y;

          if( nn > 0 ){
            line_fx12(pcenter,center, DARK_GREY, X_SCREEN_OFFSET);
            line_fx12(pleft,  left,   DARK_GREY, X_SCREEN_OFFSET);
            line_fx12(pright, right,  DARK_GREY, X_SCREEN_OFFSET);
          }

          pcenter = center;
          pleft   = left;
          pright  = right;

          const s16 iy = static_cast< s16 >( y );
          for( auto& obj : objs ){
            if( obj.state == Obj::Disappear ) continue;
            if( obj.isDrawed ) continue;

            const s16 iobjy = z2y( obj.z );
            if( iobjy >= iy && iobjy < iy + YSPAN ){
              const fx12 t2 = (iobjy - YPIX_TOP ) / YRANGE;  // TODO:
              obj.draw(t2,ox_center,xCam,iobjy);
            }
          }
        }    

        // mycar
        setz(3);
        drawMyCar();
      }break;
    }

    camera();
    setz(maxz());

    cursor(2,2);
    print( "%d km/h   ",vzCar.raw_value()>>7);
  }
public: virtual ~RaceApp(){}
};

static  RaceApp  app;

bool  Obj::chkIfCollide(){
  if( this->z > 15 )  return  false;
  if( this->z < 0  )  return  false;
  if( this->x + this->hw < app.xCar - HW_CAR )  return  false;
  if( this->x - this->hw > app.xCar + HW_CAR )  return  false;
  return  true;
}

void  Obj::update(){
  if( state == Disappear ) return;
  isDrawed = false;

  const fx12 lvz = this->vz - app.vzCar;
  this->z += lvz;

  if( chkIfCollide() ){
    if( 0 == app.cnt_crash ){
      app.flushBg.setTable( flushAnimBurnt );
      app.cnt_crash = 1;
    }
  }

  if( this->z < -20 || this->z > 400+20 ){
    state = Disappear;
  }
}

void  Obj::draw(fx12 t,fx12 x_center,fx12 xCam,fx12 y){
  if( state == Disappear ) return;
  isDrawed = true;
  if( y < YPIX_TOP )    return;
  if( y > YPIX_BOTTOM ) return;

  const fx12  width = this->hw * 2; 
  const fx12  xl = x_center + (-xCam + this->x - this->hw) * t;
  const Point ll( xl, y);
  const Point rr( xl + width * t, y);

  Color color = RED;
  //if( chkIfCollide() )  color = GREEN;

  if( this->z < 0 ) return;
  line_fx12(ll,rr,color,X_SCREEN_OFFSET);
}

int main() {
  app.run();
  return 0;
}