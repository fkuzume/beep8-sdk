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
  constexpr int NMAX_CENTER = 240 / YSPAN;

  constexpr int SPR_MYCACR_TAIL         = 48;
  constexpr int SPR_MYCACR_FRONT_WHEEL  = 32;
  constexpr int SPR_MYCACR_REAR_WHEEL   = 16;
  constexpr int SPR_SMOKE = 64;

  constexpr int OTZ_ROAD = 15;
  constexpr int OTZ_OBJ  = 14;
  constexpr int OTZ_CAR  = 3;

  constexpr fx12 YPIX_TOP    = 70;
  constexpr fx12 YPIX_BOTTOM = 150;
  constexpr fx12 W_NEAR = 200;

  constexpr fx12 VZ_ENABLE_WHEEL = fx12(1000,4096);

  constexpr fx12 EVERY_50   = 50;
  constexpr fx12 EVERY_100  = 100;

  constexpr u16 N_FIFO_MAPDATA = 32;  // must be 2^n

  constexpr fx12 HW_CAR = 20; 
  constexpr fx12 X_SCREEN_OFFSET = 64;
  constexpr fx12 MAX_VZ = fx12(10);
  constexpr fx12 VZ_FRIC_CURVED = fx12(4037,4096);
  constexpr fx12 VX_FRIC_SLIPPING = fx12(4000,4096);

  static  Xorshift32 xors;

  static  constexpr auto  flushAnimUsual  = to_array<pico8::Color>({BLACK});
  static  constexpr auto  flushAnimBurnt  = to_array<pico8::Color>({RED,BLACK,ORANGE,RED,BLACK,RED,BLACK,ORANGE,ORANGE,ORANGE,DARK_BLUE,ORANGE,RED,ORANGE,ORANGE,RED,BLACK});

  inline  const fx12 _abs(const fx12& x_ ){
    return x_ > fx12(0) ? x_ : -x_;
  }

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
  State state       = Disappear;

  enum DrawType : uint8_t {
    Nothing,
    Car,
    Pole,
    RoadsideLights,
    Signboard,
    TallSignboard,
    Bridge
  };
  DrawType drawType = Nothing;
  Color color[2];

  fx12 x = 0;
  fx12 z = 0;
  fx12 vz = 0;
  fx12 hw = 35;  // half width
  fx12 height = 0;
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
  fx12 ax_center;
  fx12 vx_center;
  fx12 vz_friction;
  bool slipping;
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
  int     cnt_every_50_distance;

  fx12    every_100_distance;
  int     cnt_every_100_distance;

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


    slipping = false;
    vz_friction = fx12(1);
    vx_center = ax_center = 0;
    xCam = xCar = vzCar = 0;
    xWheel = 0;
    acc_distance = distance = 0;

    every_50_distance = 0;
    cnt_every_50_distance = 0;

    every_100_distance = 0;
    cnt_every_100_distance = 0;

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

  void readMapData(){
    const u16 idx_0 = (upMapData - 1)  & (N_FIFO_MAPDATA - 1);
    const MapData& md_0 = mapData[ idx_0 ];

    const u16 idx_1 = upMapData & (N_FIFO_MAPDATA - 1);
    const MapData& md_1 = mapData[ idx_1 ];

    const fx12 t = distance / md_1.distance;
    ax_center = (fx12(1)-t) * md_0.ax + t * md_1.ax;
  }

  fx12  tblCenter[ NMAX_CENTER ];

  void  calcCenter(){
    int nn = 0;
    fx12 x_center = 0;
    vx_center = 0;
    const fx12 yspan( YSPAN );
    for( fx12 y=YPIX_BOTTOM ; y>YPIX_TOP ; y -= yspan , ++nn ){
      if( nn >= NMAX_CENTER ) break;

      tblCenter[ nn ] = x_center;

      vx_center += ax_center;
      x_center  += vx_center;
      vx_center += ax_center;
      x_center  += vx_center;
    }
  }

  void updatePlaying(){
    if( score >= CLEAR_SCORE ){
      reqReset = GameState::Clear;
    } else if( dead ){
      reqReset = GameState::Title;
    }

    if( cnt_crash > 0 ) cnt_crash++;

    readMapData();
    calcCenter();

    bool accel = false;
    vz_friction = fx12(1);
    if( cnt_crash == 0 ){
      fx12 vxCar = -vx_center * fx12(873,1000); 
      const fx12 abs_vx_center = _abs( vx_center );
      const fx12 ratio_abs_vx_center = abs_vx_center * fx12(1,1000);
      const bool curved = abs_vx_center != 0; 
#if 0
      const u32 btn_o = btn( BUTTON_O );
      const u32 btn_x = btn( BUTTON_X );
#else
      const u32 btn_o = false;
      const u32 btn_x = true;
#endif

      accel = btn_x ? true : false;

      if( btn( BUTTON_LEFT ) ){
        if( curved && accel ) vz_friction = VZ_FRIC_CURVED - ratio_abs_vx_center;

        vxCar = -6;
        --xWheel;
        --xWheel;
      } else if( btn( BUTTON_RIGHT ) ){
        if( curved && accel ) vz_friction = VZ_FRIC_CURVED - ratio_abs_vx_center;

        vxCar = +6;
        ++xWheel;
        ++xWheel;
      } else {
        if( xWheel < 0 ){
          ++xWheel;
        } else if ( xWheel > 0 ){
          --xWheel;
        }
      }
      xWheel = std::clamp(xWheel,-16,+16);

      if( slipping ) vxCar *= VX_FRIC_SLIPPING;

      if( vzCar > VZ_ENABLE_WHEEL ){
        xCar += vxCar;
      } else {
        xWheel = 0;
      }

      if( btn_o ){
        vzCar -= fx12(200,4096);
      } else if ( btn_x ){
        vzCar += fx12(130,4096);
      } else {
        vzCar -= fx12(23,4096);
      }
    } else {
      vzCar *= fx12(3937,4096);
    }

    vzCar *= vz_friction;
    vzCar = std::clamp(vzCar, fx12(0), MAX_VZ );
    slipping = vz_friction < fx12(1) && vzCar > fx12(2) && accel == true;

    distance           += vzCar;
    acc_distance       += vzCar;
    every_50_distance  += vzCar;
    every_100_distance += vzCar;
    if( every_50_distance > EVERY_50 ){
      every_50_distance -= EVERY_50;
      ++cnt_every_50_distance;
    }

    if( every_100_distance > EVERY_100 ){
      every_100_distance -= EVERY_100;
      ++cnt_every_100_distance;

      if( cnt_every_100_distance & 3 ){;
        auto idobj = allocObj();
        if( idobj ){
          Obj& obj = objs[ idobj.value() ];
          obj.x = W_NEAR + 30;
          obj.z = +300;
          obj.vz = 0;
          obj.drawType = Obj::Pole;
        }
      } else {
        auto idobj = allocObj();
        if( idobj ){
          Obj& obj = objs[ idobj.value() ];
#if 0
          obj.x = (W_NEAR + 30);
          obj.z = +300;
          obj.vz = 0;
          obj.drawType = Obj::RoadsideLights;
#endif
#if 1
          obj.x = (W_NEAR + 100);
          if( xors.next() & 1 ) obj.x = - obj.x;
          obj.z = +350;
          obj.vz = 0;

          static const Color colors[] = {DARK_BLUE,DARK_BLUE,DARK_GREY,DARK_PURPLE};
          obj.color[0] = rndt( colors );
          obj.color[1] = rndt( colors );

          obj.height = (xors.next() & 3) ? 70:40;

          obj.drawType = Obj::Signboard;
//obj.drawType = Obj::TallSignboard;
#endif
        }

        auto idobj_bridge = allocObj();
        if( idobj_bridge ){
          Obj& obj = objs[ idobj.value() ];
          obj.x = 0;
          obj.z = +500;
          obj.vz = 0;
          obj.drawType = Obj::Bridge;
        }
      }

      #if 0
      auto idobj = allocObj();
      if( idobj ){
        Obj& obj = objs[ idobj.value() ];
        obj.x = -40;
        obj.z = +300;
        //obj.vz = fx12(3000,4096);
        obj.vz = 0;
        obj.drawType = Obj::Car;
      }
      #endif
    }

    MapData& md = mapData[ upMapData ];
    if( distance > md.distance ){
      distance -= md.distance;
      upMapData = (upMapData + 1) & (N_FIFO_MAPDATA-1);
    }

    xCam = xCar;

    for( auto& obj : objs ){
      obj.update();
    }
  }

  void drawPlaying(){
    if( score != disp_score ){
      disp_score = score; 
      print("\e[21;1H%d",disp_score);
    }

    setz(maxz());
    line_fx12(0,YPIX_BOTTOM,128,YPIX_BOTTOM,WHITE);
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
    const fx12 yspan( YSPAN );
    for( fx12 y=YPIX_BOTTOM ; y>YPIX_TOP ; y -= yspan , ++nn ){
      const fx12 ox_center = tblCenter[ nn ];
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
        setz( OTZ_ROAD );
        line_fx12(pcenter,center, DARK_GREY, X_SCREEN_OFFSET);
        line_fx12(pleft,  left,   DARK_GREY, X_SCREEN_OFFSET);
        line_fx12(pright, right,  DARK_GREY, X_SCREEN_OFFSET);
      }

      pcenter = center;
      pleft   = left;
      pright  = right;

      setz( OTZ_OBJ );
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
    setz( OTZ_CAR );
    drawMyCar();
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

      auto xx = to_fx8( xCar - xCam + 64 - 16 );
      auto yy = to_fx8(YPIX_BOTTOM - 12);

      setz(1);

      const int lxWheel  = xWheel>>2;
      const int lxWheel2 = xWheel>>3;

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

      int lxBody = 0;
      if( lxWheel2 < 0 ){
        lxBody = -1;
      } else if ( lxWheel2 > 0 ){
        lxBody = +1;
      }
      spr(
        SPR_MYCACR_TAIL,
        xx+lxBody,yy+yoff_bd,
        2,1
      );
      spr(
        SPR_MYCACR_TAIL,
        xx+16+lxBody,yy+yoff_bd,
        2,1,
        true
      );

      const fx8 lx_wheel = xx-lxWheel2;
      spr(
        SPR_MYCACR_REAR_WHEEL+anm,
        lx_wheel,yy+3-yoff
      );

      const fx8 rx_wheel = lx_wheel + 24;
      spr(
        SPR_MYCACR_REAR_WHEEL+anm,
        rx_wheel,yy+3-yoff
      );

      static  Xorshift32 xors_smoke;
      if( slipping ){
        fx8 x_smoke = xWheel > 0 ? lx_wheel-1-(xors_smoke.next()&7) : rx_wheel+1+(xors_smoke.next()&7);
        spr(
          SPR_SMOKE + (xors_smoke.next()&3) ,
          x_smoke,yy+3-yoff,
          1,1,
          xors_smoke.next()&1 ? true:false,
          false
        );
      }
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
      }break;
      case  GameState::Nil:
      case  GameState::Title:{
        camera();
      }break;
      case  GameState::Playing:{
        drawPlaying();
      }break;
    }
    setz(1);
    camera();
    setz(maxz());

    cursor(2,2);
    print( "%d km/h   ",vzCar.raw_value()>>7);
  }
public: virtual ~RaceApp(){}
};

static  RaceApp  app;

bool  Obj::chkIfCollide(){
return false;
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

  if( this->z < -20 || this->z > 350+20 ){
    state = Disappear;
  }
}

void  Obj::draw(fx12 t,fx12 x_center,fx12 xCam,fx12 y){
  if( state == Disappear ) return;
  isDrawed = true;
  if( y < YPIX_TOP )    return;
  if( y > YPIX_BOTTOM ) return;
  if( this->z < 0 )     return;

  switch( drawType ){
    case  Car:{
      const fx12  width = this->hw * 2; 
      const fx12  xl = x_center + (-xCam + this->x - this->hw) * t;
      const Point ll( xl, y);
      const Point rr( xl + width * t, y);
      line_fx12(ll,rr,RED,X_SCREEN_OFFSET);
    }break;

    case  Pole:{
      const fx12  xl = x_center + (-xCam + this->x) * t;
      const Point p0(xl,y);
      const Point p1(xl,y-30*t);
      line_fx12(p0,p1,WHITE,X_SCREEN_OFFSET);
    }break;

    case  RoadsideLights:{
      const fx12  xl = x_center + (-xCam + this->x) * t;
      const Point p0(xl,y);
      const Point p1(xl,        y-200*t);
      fx12 xt = this->x > 0 ? t : -t;
      const Point p2(p1.x-90*xt, p1.y );
      line_fx12(p0,p1,WHITE,X_SCREEN_OFFSET);
      line_fx12(p1,p2,WHITE,X_SCREEN_OFFSET);

      rectfill_fx12(
        p2.x - 15*t,
        p2.y,

        p2.x + 15*t,
        p2.y + 10*t,

        YELLOW,
        X_SCREEN_OFFSET
      );
    }break;
    
    case  Signboard:{
      const fx12  xl = x_center + (-xCam + this->x) * t;

      const fx12 tx40 = 40 * t;
      const fx12 tx20 = 20 * t;

      line_fx12(
        xl - tx40,
        y  - tx20,

        xl - tx40,
        y,
        color[1],
        X_SCREEN_OFFSET
      );

      line_fx12(
        xl + tx40,
        y  - tx20,

        xl + tx40,
        y,
        color[1],
        X_SCREEN_OFFSET
      );

      const fx12 tx60 = 60 * t;
      rectfill_fx12(
        xl - tx60,
        y - this->height*t,

        xl + tx60,
        y - 10*t,

        color[0],
        X_SCREEN_OFFSET
      );
    }break;

    case  TallSignboard:{
      const fx12  xl = x_center + (-xCam + this->x) * t;
      const fx12  tx8   = 8 * t;
      const fx12  tx100 = 100 * t;
      const fx12  tx130 = 130 * t;

      rectfill_fx12(
        xl - tx8,
        y  - tx130,

        xl + tx8,
        y,

        color[1],
        X_SCREEN_OFFSET
      );

      rectfill_fx12(
        xl - tx100,
        y  - 200*t,

        xl + tx100,
        y  - tx130,

        color[0],
        X_SCREEN_OFFSET
      );
    }break;

    case  Bridge:{
      const fx12  height = 130 * t;
      const fx12  xl = x_center + (-xCam + this->x) * t;
      const fx12 width = W_NEAR *  fx12(1200,1000);

      line_fx12(
        xl - width*t,
        y  - height ,

        xl + width*t,
        y  - height ,
        DARK_GREY,
        X_SCREEN_OFFSET
      );

      line_fx12(
        xl - width*t,
        y  - height ,

        xl - width*t,
        y,
        DARK_GREY,
        X_SCREEN_OFFSET
      );

      line_fx12(
        xl + width*t,
        y  - height ,

        xl + width*t,
        y,
        DARK_GREY,
        X_SCREEN_OFFSET
      );
    }break;

    case  Nothing:
      break;
  }
}

int main() {
  app.run();
  return 0;
}