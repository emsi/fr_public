// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Exec_Effect_PartEmitter and Exec_Effect_PartSystem, geneffect.cpp.
#include "wz3/effect_particles.hpp"
#include "wz3/texture.hpp"
#include "particle_random.hpp"
#include <cfenv>
namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Nearest {int previous=std::fegetround();Nearest(){std::fesetround(FE_TONEAREST);}~Nearest(){std::fesetround(previous);}};
float real(std::span<const std::uint32_t> p,unsigned i) {
  const auto v=as_float(p[i]);require(std::isfinite(v) && std::abs(v)<=1e6f,"invalid effect particle parameter");return v;
}
std::int32_t integer(float v) {
  require(std::isfinite(v) && double(v)>=-2147483648.0 && double(v)<=2147483647.0,"particle integer overflow");return std::int32_t(v);
}
std::int32_t fraction(std::int32_t a,std::int32_t b) {require(b>0,"invalid particle fade duration");return std::int32_t(std::int64_t(a)*65536/b);}
}
struct EffectParticles::Impl {
  struct Slot {unsigned capacity{};std::vector<EffectParticle> particles;};
  struct Emitter {std::int64_t burst{};unsigned next{};};
  std::array<Slot,16> slots;std::map<std::size_t,Emitter> emitters;
  std::shared_ptr<ParticleRandom> random;
  explicit Impl(std::shared_ptr<ParticleRandom> r):random(r ? std::move(r) : std::make_shared<ParticleRandom>()) {}
};
EffectParticles::EffectParticles(std::shared_ptr<ParticleRandom> r):impl(std::make_unique<Impl>(std::move(r))) {}
EffectParticles::~EffectParticles()=default;
void EffectParticles::initialize(unsigned slot,unsigned count) {
  require(slot<16 && count && count<=65536,"invalid particle slot/capacity");auto& s=impl->slots[slot];s.capacity=count;s.particles.clear();s.particles.reserve(count);
}
std::span<const EffectParticle> EffectParticles::particles(unsigned slot) const {
  require(slot<16,"invalid particle slot");return impl->slots[slot].particles;
}
void EffectParticles::emit(std::size_t instance,std::span<const std::uint32_t> parameters,const SceneMatrix& world,const FrameClock& clock) {
  const Nearest rounding;require(clock.delta>=0 && clock.current>=clock.delta,"invalid particle clock interval");
  // Saved v13/v14 prefixes are copied onto current editor defaults.
  require(parameters.size()>=17 && parameters.size()<=20,"unsupported PartEmitter layout");
  std::array<std::uint32_t,20> p{};p[17]=1;std::copy(parameters.begin(),parameters.end(),p.begin());
  const auto slot=p[0],mode=p[1],burst=p[2],slots=p[17];
  require(slot<16 && slots && slots<=16-slot && !(mode&~0x10fu) && burst<=2147483647,"unsupported PartEmitter mode/slots/burst");
  for(unsigned i=3;i<20;++i)if(i!=17)real(p,i);
  require(real(p,3)>0 && real(p,4)>=0,"invalid particle lifetime/rate");const auto lifetime=integer(real(p,3)*1000);require(lifetime>0,"particle lifetime below one millisecond");
  require(impl->emitters.size()<65536 || impl->emitters.contains(instance),"emitter instance limit");
  auto [it,inserted]=impl->emitters.try_emplace(instance,Impl::Emitter{burst,0});static_cast<void>(inserted);auto& mem=it->second;
  auto count=std::int64_t(integer(float(clock.current&0xffffff)*(real(p,4)/1000)))-integer(float((clock.current-clock.delta)&0xffffff)*(real(p,4)/1000));
  for(unsigned s=0;s<slots;++s) {const auto& dest=impl->slots[slot+s];count=std::min(count,std::int64_t(dest.capacity-dest.particles.size()));}
  if(mode&0x100) {count=0;mem.burst=burst;}
  if(burst) {count=std::min(count,mem.burst);mem.burst-=count;}
  require(mem.next<slots,"animated emitter slot count invalidates instance cursor");auto& rng=*impl->random;
  for(std::int64_t i=0;i<count;++i) {
    EffectParticle v;v.lifetime=lifetime;
    for(unsigned c=0;c<3;++c)v.speed[c]=world[c]*real(p,5)+world[4+c]*real(p,6)+world[8+c]*real(p,7);
    v.speed[3]=real(p,8);
    // Explicit right-to-left order for the three Init arguments used by the
    // old x86 compiler. Exact executable RNG/capture comparison is pending.
    auto rv=rng.sphere(true);if(mode&1)rv.Unit3();
    for(unsigned c=0;c<3;++c) {if(mode&8)v.speed[c]*=rv[c];v.position[c]=rv[c]*real(p,9+c)+world[12+c];}
    v.position[3]=(rng.real()*2-1)*real(p,12)+legacy::sPI2F/8;
    rv=rng.sphere(true);if(mode&2)rv.Unit3();
    for(unsigned c=0;c<3;++c)v.speed[c]+=rv[c]*real(p,13+c);
    v.speed[3]+=(rng.real()*2-1)*real(p,16);for(auto& value:v.speed)value*=.001f;
    impl->slots[slot+mem.next].particles.push_back(v);mem.next=(mem.next+1)%slots;
  }
}
std::vector<SceneEffectVertex> EffectParticles::draw(std::span<const std::uint32_t> p,const SceneCamera& camera,const SceneMatrix& world,const FrameClock& clock) {
  const Nearest rounding;require(clock.delta>=0 && clock.current>=clock.delta,"invalid particle clock interval");require(p.size()==12 && p[0]<16 && !(p[11]&~1u),"unsupported PartSystem layout/flags");
  for(unsigned i=5;i<=10;++i)real(p,i);
  require(real(p,8)>=0 && real(p,8)<=1 && real(p,9)>=0 && real(p,9)<=.5f,"invalid particle middle/fade");
  auto& particles=impl->slots[p[0]].particles;std::vector<SceneEffectVertex> output;output.reserve(particles.size()*4);
  legacy::sVector right,up;
  if(p[11]&1) {right.Init(world[0],world[4],world[8]);up.Init(world[1],world[5],world[9]);}
  else {right.Init(camera.world[0],camera.world[1],camera.world[2]);up.Init(camera.world[4],camera.world[5],camera.world[6]);}
  right.Unit3();up.Unit3();const auto c0=color64(p[2]),c1=color64(p[3]),c2=color64(p[4]);
  const Pixel black{0,0,0,65535};
  for(std::size_t i=0;i<particles.size();) {
    auto& v=particles[i];const auto middle=integer(v.lifetime*(1022*real(p,8)+1)/1024);
    std::int32_t percent;Pixel color;float size;
    if(v.age<middle) {percent=fraction(v.age,middle);color=fade64(c0,c1,percent);size=real(p,5)+percent/65536.0f*(real(p,6)-real(p,5));}
    else {percent=fraction(v.age-middle,v.lifetime-middle);color=fade64(c1,c2,percent);size=real(p,6)+percent/65536.0f*(real(p,7)-real(p,6));}
    auto fade=65535;const auto fadetime=integer(v.lifetime*(1022*real(p,9)+1)/1024);
    if(v.age<fadetime)fade=fraction(v.age,fadetime);
    if(v.age>v.lifetime-fadetime)fade=fraction(v.lifetime-v.age,fadetime);
    color=fade64(black,color,fade);
    // SetVert's original alpha shift is five bits, not the RGB seven.
    const auto packed=((std::uint32_t(color[3]>>5)&255)<<24)|(std::uint32_t(color[0]>>7)<<16)|(std::uint32_t(color[1]>>7)<<8)|(color[2]>>7);
    const float ss=std::sin(v.position[3])*size,sc=std::cos(v.position[3])*size;
    const float offsets[4][2]={{sc,ss},{ss,-sc},{-sc,-ss},{-ss,sc}};
    constexpr float uv[4][2]={{0,0},{0,1},{1,1},{1,0}};
    for(unsigned k=0;k<4;++k) {SceneEffectVertex vertex;vertex.color=packed;vertex.uv={uv[k][0],uv[k][1]};for(unsigned c=0;c<3;++c)vertex.position[c]=v.position[c]+right[c]*offsets[k][0]+up[c]*offsets[k][1];output.push_back(vertex);}
    for(unsigned c=0;c<4;++c)v.position[c]+=v.speed[c]*clock.delta;
    const auto gravity_steps=std::int64_t(clock.delta)*(std::int64_t(clock.delta)+1)/2;
    v.position[1]-=float(gravity_steps)*real(p,10)/31623.0f;v.speed[1]-=clock.delta*real(p,10)/31623.0f;
    const auto age=std::int64_t(v.age)+clock.delta;
    if(age>v.lifetime) {v=particles.back();particles.pop_back();}else {v.age=std::int32_t(age);++i;}
  }
  return output;
}
} // namespace wz3
