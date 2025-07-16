#include <pico8.h>
#include "tbl_stepper.h"

using namespace std;  
using namespace pico8;  
/*
  TODO: TITLE画面
  TODO: 追い抜いた車のScore
  TODO: HI-Score
  TODO: 左右崖
  TODO: 表示優先順位
  TODO: CenterLine
  TODO: FakeFog
  TODO: ブレーキ
*/
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
  constexpr int SPR_MOON = 80;
  constexpr int SPR_TITLE = 7*16;

  constexpr int OTZ_ROAD = 15;
  constexpr int OTZ_OBJ  = 14;
  constexpr int OTZ_CAR  = 3;
  constexpr int OTZ_TITLE  = 1;

  constexpr fx12 YPIX_TOP    = 60;
  //constexpr fx12 YPIX_TOP    = 66;
  constexpr fx12 YPIX_BOTTOM = 150;
  constexpr fx12 W_NEAR = 200;

  constexpr fx12 VZ_ENABLE_WHEEL = fx12(1000,4096);
  constexpr fx12 VZ_OUTSIDE_FRICTION = fx12(4000,4096);

  constexpr fx12 EVERY_50   = 50;
  constexpr fx12 EVERY_100  = 100;

  constexpr fx12 Z_CLIP_NEAR = -20;
  constexpr fx12 Z_CLIP_FAR  = 550;

  constexpr u16 N_FIFO_MAPDATA = 32;  // must be 2^n

  constexpr fx12 HW_CAR = 20; 
  constexpr fx12 X_SCREEN_OFFSET = 64;
  constexpr fx12 MAX_VZ = fx12(8);
  constexpr fx12 VZ_FRIC_CURVED = fx12(1);
  constexpr fx12 VX_FRIC_SLIPPING = fx12(4000,4096);

  static  Xorshift32 xors;

  static  constexpr auto  flushAnimUsual  = to_array<pico8::Color>({BLACK});
  static  constexpr auto  flushAnimBurnt  = to_array<pico8::Color>({RED,BLACK,ORANGE,RED,BLACK,RED,BLACK,ORANGE,ORANGE,ORANGE,DARK_BLUE,ORANGE,RED,ORANGE,ORANGE,RED,BLACK});

  struct  Point {
    fx12 x;
    fx12 y;
    Point(fx12 x_,fx12 y_ )
      : x(x_), y(y_)
    {}
    Point(){}

    Point operator-(Point const& rhs) const {
      return Point{x - rhs.x, y - rhs.y};
    }

    Point operator+(Point const& rhs) const {
      return Point{x + rhs.x, y + rhs.y};
    }

    Point& operator+=(Point const& rhs) {
      x += rhs.x;
      y += rhs.y;
      return *this;
    }
    Point operator*(fx12 s) const {
      return Point{x * s, y * s};
    }

    Point& operator*=(fx12 s) {
      x *= s;
      y *= s;
      return *this;
    }
  };

  inline Point operator*(fx12 s, Point const& p) {
    return p * s;
  }

  static  bool  every(int x,int mask){
    return (!(x & mask)) ? true : false;
  }

  inline  const fx12 _abs(const fx12& x_ ){
    return x_ > fx12(0) ? x_ : -x_;
  }

  inline  const fx12 _sgn(fx12 x) {
    return (x > fx12(0)) ? fx12(1) : (x < fx12(0)) ? fx12(-1) : fx12(1);
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

  inline  void rectfill_fx12(const Point& p0 , const Point p1 , Color color,fx12 sx=0) {
    rectfill_fx12(p0.x, p0.y, p1.x, p1.y, color, sx);
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

  constexpr int NTBL = 500;
  static  s16 tblz2y[ NTBL ];

  void  genTableZ2Y(){
    constexpr fx12  range(105);
    constexpr fx12  nume(10000);
    constexpr fx12  a(2);
    const fx12  div = nume / range;
    for( int z=0 ; z<NTBL ; ++z ){
      const fx12  zz(z);
      tblz2y[ z ] = static_cast< s16 >( YPIX_BOTTOM - ( range - nume/(a*zz+div) ) );
    }
  }

  s16   z2y( fx12 z ){
    int iz = static_cast< int >( z );
    if( iz < 0 )      iz = 0;
    if( iz > NTBL-1 ) iz = NTBL-1;
    #if 0
    return tblz2y[ iz ];
    #else
    return tblz2y[ std::clamp( iz,0,NTBL-1) ];
    #endif
  }

} // local namespace

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
    Bridge,
    RoadReflector
  };
  DrawType drawType = Nothing;

  enum CatType : uint8_t {
    TailLine,
    TailLR0,
    TailLR1,
    Truck0,
  };
  CatType carType = TailLine;
  Color color[2];

  fx12 x = 0;
  fx12 z = 0;
  fx12 vz = 0;
  fx12 hw = 35;  // half width
  fx12 height = 0;
  bool  isCollidable = false;  
  bool  isDrawed = false;
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
  fx8 x_title; 
  fx12 ax_center;
  fx12 vx_center;
  fx12 vz_friction;
  fx12 star_x_center;

  bool  slipping;
  bool  brake;
  bool  out_of_roadside;
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
    cnt_title = 0;

    flushBg.setTable( flushAnimUsual );

    slipping = false;
    brake = false;
    out_of_roadside = false;
    vz_friction = fx12(1);
    vx_center = ax_center = 0;
    star_x_center = 0;
    xCam = xCar = vzCar = 0;
    xWheel = 0;
    acc_distance = distance = 0;

    every_50_distance = 0;
    cnt_every_50_distance = 0;

    upMapData = 1;
    for( u16 nn=0 ; nn < N_FIFO_MAPDATA ; ++nn ){
      MapData& md = mapData[ nn ];
      md.distance = rndf12(130,500);
      if( nn <= 2 ){
        md.ax = 0;
      } else {
        //md.ax = rndf12( fx12(-19,100), fx12(+19,100) );
        //md.ax = rndf12( fx12(-1,100), fx12(+1,100) );
        md.ax = rndf12( fx12(-10,100), fx12(+10,100) );
        //md.ax = 0;
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

  void appearPole(){
    auto idobj = allocObj();
    if( !idobj )  return;

    Obj& obj = objs[ idobj.value() ];
    obj.drawType = Obj::Pole;
    obj.x = W_NEAR + 30;
    if( xors.next() & 1)  obj.x = -obj.x;
    obj.z = +500;
    obj.vz = 0;
  }

  void appearRoadsideLights(){
    auto idobj = allocObj();
    if( !idobj )  return;

    Obj& obj = objs[ idobj.value() ];
    obj.x = (W_NEAR + 30);
    obj.z = +300;
    obj.vz = 0;
    obj.drawType = Obj::RoadsideLights;
  }

  void appearSignboard(){
    auto idobj = allocObj();
    if( !idobj )  return;
    Obj& obj = objs[ idobj.value() ];

    obj.x = (W_NEAR + 100);
    if( xors.next() & 1 ) obj.x = - obj.x;
    obj.z = +500;
    obj.vz = 0;

    static const Color colors[] = {DARK_BLUE,DARK_BLUE,DARK_GREY,DARK_PURPLE};
    obj.color[0] = rndt( colors );
    obj.color[1] = rndt( colors );

    obj.height = (xors.next() & 3) ? 70:40;

    static const  Obj::DrawType dt[] = { Obj::Signboard,Obj::Signboard,Obj::Signboard,Obj::TallSignboard };
    obj.drawType = rndt( dt );
  }

  void appearBridge(){
    auto idobj = allocObj();
    if( !idobj )  return;

    Obj& obj = objs[ idobj.value() ];
    obj.x = 0;
    obj.z = +470;
    obj.vz = 0;
    obj.drawType = Obj::Bridge;
  }

  void  appearRoadReflector(){
    if( abs( ax_center.raw_value() ) < 100 )  return;

    auto idobj = allocObj();
    if( !idobj )  return;

    Obj& obj = objs[ idobj.value() ];
    obj.x = (W_NEAR + 43) * (-_sgn( ax_center ));
    obj.z = +200;
    obj.vz = 0;
    obj.drawType = Obj::RoadReflector;
  }

  void  appearCar(){
    auto idobj = allocObj();
    if( !idobj )  return;

    Obj& obj = objs[ idobj.value() ];
    obj.isCollidable = true;
    obj.x = xors.next() & 1 ? -53 : +53;
    obj.z = +500;
    obj.vz = fx12(2);
    obj.drawType = Obj::Car;

    static const Obj::CatType carTypes[] = {
#if 0
      Obj::TailLine, Obj::TailLR0,
#else
      //Obj::TailLR0, Obj::TailLR0,
      //Obj::TailLR1, Obj::TailLR1,
      Obj::Truck0, Obj::Truck0,
#endif
    };

    static const Color colors[] = {
      DARK_BLUE,DARK_BLUE,
      DARK_BLUE,DARK_BLUE,
      DARK_BLUE,DARK_BLUE,
      DARK_BLUE,DARK_BLUE,
      BLACK, BLACK,
      BLACK, BLACK,
      DARK_PURPLE,DARK_GREY,DARK_GREY
    };
    obj.color[0]  = rndt( colors );
    obj.carType   = rndt( carTypes );
  }

  void every50(){
    if( every(cnt_every_50_distance,3) ){
      appearPole();
    }

    if( every(cnt_every_50_distance,7) ){
      appearRoadsideLights();
    }

    if( every( xors.next(), 7) ){
      appearSignboard();
    }

    if( every(cnt_every_50_distance,7) ){
      appearCar();
    }

    if( every( xors.next(), 63) ){
      appearBridge();
    }

    appearRoadReflector();
  }

  void updateMyCar(){
    brake = false;
    bool accel = false;
    vz_friction = fx12(1);
    bool rot_handle = false;

    if( cnt_crash == 0 ){
      fx12 vxCar = -vx_center * fx12(873,1000); 
      const fx12 abs_vx_center = _abs( vx_center );
      const fx12 ratio_abs_vx_center = abs_vx_center * fx12(2,1000);
      const bool curved = abs_vx_center != 0; 
#if 0
      const u32 btn_o = btn( BUTTON_O );
      const u32 btn_x = btn( BUTTON_X );
#else
      const u32 btn_o = btn( BUTTON_O );
      const u32 btn_x = true;
#endif
      accel = btn_x ? true : false;

      bool l_or_r = false;
      if( btn( BUTTON_LEFT ) ){
        l_or_r = true;
        vxCar = -8;
        --xWheel;
        --xWheel;
      } else if( btn( BUTTON_RIGHT ) ){
        l_or_r = true;
        vxCar = +8;
        ++xWheel;
        ++xWheel;
      } else {
        if( xWheel < 0 ){
          ++xWheel;
        } else if ( xWheel > 0 ){
          --xWheel;
        }
      }

      if( l_or_r ){
        if( curved && accel ) vz_friction = VZ_FRIC_CURVED - ratio_abs_vx_center;
        rot_handle = true;
      }

      xWheel = std::clamp(xWheel,-18,+18);

      if( slipping ) vxCar *= VX_FRIC_SLIPPING;

      if( vzCar > VZ_ENABLE_WHEEL ){
        xCar += vxCar;
      } else {
        xWheel = 0;
      }

      if( btn_o ){
        vzCar -= fx12(200,4096);
        brake = true;
      } else if ( btn_x ){
        vzCar += fx12(130,4096);
      } else {
        vzCar -= fx12(23,4096);
      }
    } else {
      vzCar *= fx12(3937,4096);
    }

    vzCar *= vz_friction;

    out_of_roadside = _abs( xCar ) > W_NEAR + 7 && vzCar > fx12(1);
    if( out_of_roadside ) vzCar *= VZ_OUTSIDE_FRICTION;

    if( rot_handle ){
      vzCar *= fx12(4071,4096);
    }

    vzCar = std::clamp(vzCar, fx12(0), MAX_VZ );
    slipping = vz_friction < fx12(1) && vzCar > fx12(2) && accel == true;
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
    updateMyCar();

    distance           += vzCar;
    acc_distance       += vzCar;
    every_50_distance  += vzCar;
    if( every_50_distance > EVERY_50 ){
      every_50_distance -= EVERY_50;
      ++cnt_every_50_distance;
      every50();
    }

    MapData& md = mapData[ upMapData ];
    if( distance > md.distance ){
      distance -= md.distance;
      upMapData = (upMapData + 1) & (N_FIFO_MAPDATA-1);
    }

    xCam = xCar;

    for( auto& obj : objs ) obj.update();
  }

  void drawPlaying(){
    if( score != disp_score ){
      disp_score = score; 
      cursor(12-3,20,BG_PAL_3);
      print("SC:%d",disp_score);
    }

    setz(maxz());
    line_fx12(0,YPIX_BOTTOM,128,YPIX_BOTTOM,WHITE);
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
    const fx12 YRANGE = YPIX_BOTTOM - YPIX_TOP;
    for( fx12 y=YPIX_BOTTOM ; y>=YPIX_TOP ; y -= YSPAN , ++nn ){
      const fx12 ox_center = tblCenter[ nn ];
      const fx12 tt     = (y - YPIX_TOP ) / YRANGE;
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
    x_title = -500;
    print("\e[2J");
    print("\e[3;7H    ");
    print("\e[3q\e[13;4H HI:%d\e[0q" , hi_score );
    print("\e[15;4H SC:%d", score );

    print("\e[18;4H", score );

    int yy=19;
    print("\e[%d;1HBRAKE: Z/X KEY", yy); yy += 2;
    print("\e[%d;1HTURN:  ARROW", yy); yy += 1;
    print("\e[%d;1H       SWIPE", yy); yy += 2;

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
    x_title += -x_title/11;
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
    auto xx = to_fx8(xCar - xCam + 64 - 16 );
    auto yy = to_fx8(YPIX_BOTTOM - 12);
    if( cnt_crash == 0 ){
      setz(1);

      const int lxWheel  = xWheel>>1;
      const int lxWheel2 = xWheel>>2;

      const u32 uacc_distance = static_cast< u32 >( acc_distance );
      const u32 anm   = (uacc_distance>>4) & 1;
      u32 yoff  = (uacc_distance>>5) & 1;
      if( brake ){
        --yoff;
        yoff += xors.next()&3;
      }

      u32 yoff_bd  = 0;
      if( ((uacc_distance+77) & 0xff) == 0 ){
        ++yoff_bd;
      }

      if( brake ) yoff_bd += xors.next()&1;

      if( out_of_roadside ){
        yoff_bd -= 2;  
        yoff_bd += xors.next()&3;
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
      if( out_of_roadside ){
        if( xors.next() & 1 ) ++lxBody;
        if( xors.next() & 1 ) --lxBody;
      }

      if( lxWheel2 < 0 ){
        lxBody = -1;
      } else if ( lxWheel2 > 0 ){
        lxBody = +1;
      }
      if( slipping ){
        --lxBody; 
        lxBody += xors.next()&3;
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

      bool lr[2] = {false,false};

      if( out_of_roadside ){
        lr[0] = xors.next() & 3 ? true : false;
        lr[1] = xors.next() & 3 ? true : false;
      }

      if( slipping )  lr[ xWheel > 0 ? 0:1 ] = true;
      if( brake )     lr[0] = lr[1] = true;

      for( int ii=0 ; ii<2 ; ++ii ){
        if( ! lr[ii] )  continue;

        const fx8 x_smoke = ii == 0 ? lx_wheel-1-(xors.next()&7) : rx_wheel+1+(xors.next()&7);
        spr(
          SPR_SMOKE + (xors.next()&3) ,
          x_smoke,yy+3-yoff,
          1,1,
          xors.next()&1 ? true:false,
          false
        );
      }
    } else {
      static const Color colors[] = {RED,RED,RED,YELLOW,YELLOW,WHITE,BROWN,ORANGE};

      constexpr int mx = 50;
      int cnt = std::min(cnt_crash,mx);

      if( cnt < mx ){
        for( int nn=0 ; nn< 7 ; ++nn){
          rectfill(
            rndf(xx-7-cnt,xx+7+cnt)+4,
            rndf(yy-5-cnt, yy+cnt)+4,

            rndf(xx+16-7-cnt,xx+16+7+cnt)+4,
            rndf(yy+1-cnt, yy+4+cnt)+4,
            
            rndt( colors )
          );
        }
      }
    }

    if( cnt_crash > 2*60 ){
      reqReset = GameState::Title;
    }
  }

  void  drawStars(){
    Xorshift32 xors_stars;
    static  constexpr size_t  NSTARS = 17;
    star_x_center -= vx_center * vzCar * fx12(200,1000); 

    static  constexpr Color tbl[] = {
      DARK_BLUE,DARK_GREY , DARK_BLUE, DARK_BLUE
    };

    for( size_t ns=0 ; ns<NSTARS ; ++ns ){
      pset(
        (-(cnt_title>>1) + ((static_cast< uint32_t >( star_x_center ) + xors_stars.next()) >> (ns&3))) & 127,
        qmod( xors_stars.next() , YPIX_TOP-7 ),
        tbl[ ns & 3 ]
      );
      if( ns == NSTARS>>1 && ((frame>>6) &1) )  xors_stars.next();
    } 

    // moon
    spr(SPR_MOON,static_cast<int>(star_x_center>>2)+37+(((-cnt_title>>1)&0xff)-50),13,2,2);
  }

  void  drawTitle(){
    setz( OTZ_TITLE );
    spr(SPR_TITLE,x_title,    0,16,7);
    spr(SPR_TITLE,x_title+128,0,16,7);

    if( ((cnt_title>>2) & 3) && (cnt_title>60) ){
      scursor(64-20,70,YELLOW);
      sprint( "[TAP]" );
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
        drawTitle();
      }break;
      case  GameState::Playing:{
        drawPlaying();
        setz(1);
        camera();
        cursor(0,20,BG_PAL_2);
        print( "%d km/h ",vzCar.raw_value()>>7);
      }break;
    }
    setz(1);
    camera();
    setz(maxz());
    drawStars();

  }
public: virtual ~RaceApp(){}
};

static  RaceApp  app;

bool  Obj::chkIfCollide(){
  if( !isCollidable ) return  false;
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

  if( this->z < Z_CLIP_NEAR || this->z > Z_CLIP_FAR ){
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
      switch( carType ){
        case  TailLine:{
          const Point p0_body = ll + Point(-3,-13) * t;
          const Point p1_body = rr + Point(+3,+13) * t;
          rectfill_fx12(p0_body,p1_body,color[0],X_SCREEN_OFFSET);
          line_fx12(ll,rr,RED,X_SCREEN_OFFSET);
        }break;

        case  TailLR0:{
          const Point p0_body = ll + Point(-3,-27) * t;
          const Point p1_body = rr + Point(+3, -1) * t;
          rectfill_fx12(p0_body,p1_body,color[0],X_SCREEN_OFFSET);

          static  const Point aa =  Point(+5,-11) + Point(-4,-5);
          const Point l_red_p0 = ll + aa * t;
          static  const Point bb =  Point(+5,-11) + Point(+4,+5);
          const Point l_red_p1 = ll + bb * t;
          rectfill_fx12(l_red_p0,l_red_p1,RED,X_SCREEN_OFFSET);

          static  const Point cc = Point(-5,-11) + Point(-4,-5);
          const Point r_red_p0 = rr + cc * t;
          static  const Point dd = Point(-5,-11) + Point(+4,+5);
          const Point r_red_p1 = rr + dd * t;
          rectfill_fx12(r_red_p0,r_red_p1,RED,X_SCREEN_OFFSET);
        }break;

        case  TailLR1:{
          const Point p0_body = ll + Point(-5,-35) * t;
          const Point p1_body = rr + Point(+5, -1) * t;
          rectfill_fx12(p0_body,p1_body,color[0],X_SCREEN_OFFSET);

          const Point p2_body = ll + Point(+10,-43) * t;
          const Point p3_body = rr + Point(-10,-30) * t;
          rectfill_fx12(p2_body,p3_body,color[0],X_SCREEN_OFFSET);

          static  const Point aa =  Point(+5,-13) + Point(-4,-5);
          Point l_red_p0 = ll + aa * t;
          static  const Point bb =  Point(+5,-13) + Point(+4,+5);
          Point l_red_p1 = ll + bb * t;

          const fx12 dist = 10*t;

          rectfill_fx12(l_red_p0,l_red_p1,RED,X_SCREEN_OFFSET);
          l_red_p0.x += dist;
          l_red_p1.x += dist;
          rectfill_fx12(l_red_p0,l_red_p1,RED,X_SCREEN_OFFSET);

          static  const Point cc = Point(-5,-13) + Point(-4,-5);
          Point r_red_p0 = rr + cc * t;
          static  const Point dd = Point(-5,-13) + Point(+4,+5);
          Point r_red_p1 = rr + dd * t;
          rectfill_fx12(r_red_p0,r_red_p1,RED,X_SCREEN_OFFSET);
          r_red_p0.x -= dist;
          r_red_p1.x -= dist;
          rectfill_fx12(r_red_p0,r_red_p1,RED,X_SCREEN_OFFSET);
        }break;

        case  Truck0:{
          const Point p0_body = ll + Point(-3,-75) * t;
          const Point p1_body = rr + Point(+3, -7) * t;
          rectfill_fx12(p0_body,p1_body,color[0],X_SCREEN_OFFSET);

          static  const Point aa =  Point(+5,-2) + Point(-4,-5);
          const Point l_red_p0 = ll + aa * t;
          static  const Point bb =  Point(+5,-2) + Point(+4,+7);
          const Point l_red_p1 = ll + bb * t;
          rectfill_fx12(l_red_p0,l_red_p1,RED,X_SCREEN_OFFSET);

          static  const Point cc = Point(-5,-2) + Point(-4,-5);
          const Point r_red_p0 = rr + cc * t;
          static  const Point dd = Point(-5,-2) + Point(+4,+7);
          const Point r_red_p1 = rr + dd * t;
          rectfill_fx12(r_red_p0,r_red_p1,RED,X_SCREEN_OFFSET);
        }break;
      }
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
      const Point p1(xl,        y-150*t);
      fx12 xt = this->x > 0 ? t : -t;
      const Point p2(p1.x-77*xt, p1.y );
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
      const fx12  tx40 = 40 * t;
      const fx12  tx20 = 20 * t;

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

    case  RoadReflector:{
      const fx12  xl = x_center + (-xCam + this->x) * t;
      const Point p0(xl,y);
      const Point p1(xl,y-27*t);

      line_fx12(p0,p1,WHITE,X_SCREEN_OFFSET);

      Point a0 = p1 - Point(5,7)*t;
      Point a1 = p1 + Point(5,7)*t;
      rectfill_fx12(a0,a1,ORANGE,X_SCREEN_OFFSET);
    }break;

    case  Bridge:{
      const fx12  hi = 130 * t;
      const fx12  xl = x_center + (-xCam + this->x) * t;
      const fx12  width = W_NEAR *  fx12(1200,1000);

      line_fx12(
        xl - width*t,
        y  - hi ,

        xl + width*t,
        y  - hi ,
        DARK_GREY,
        X_SCREEN_OFFSET
      );

      line_fx12(
        xl - width*t,
        y  - hi ,

        xl - width*t,
        y,
        DARK_GREY,
        X_SCREEN_OFFSET
      );

      line_fx12(
        xl + width*t,
        y  - hi ,

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