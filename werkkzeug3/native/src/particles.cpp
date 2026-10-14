// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// References: Exec_Scene_Particles in genscene.cpp, sGetRnd/sSetRndSeed and
// sMatrix::InitDir in _types.cpp. Retain source quirks rather than "fixing" art.
#include "wz3/particles.hpp"
#include "mesh_kernel.hpp"
#include "particle_random.hpp"
#include <bit>
#include <cfenv>

namespace wz3 {
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Nearest {
  int previous=std::fegetround();
  Nearest() {std::fesetround(FE_TONEAREST);}
  ~Nearest() {std::fesetround(previous);}
};
SceneMatrix matrix(const legacy::sMatrix& m) {
  return {m.i.x,m.i.y,m.i.z,m.i.w,m.j.x,m.j.y,m.j.z,m.j.w,m.k.x,m.k.y,m.k.z,m.k.w,m.l.x,m.l.y,m.l.z,m.l.w};
}
}
struct SceneParticles::Impl {
  const SplineLibrary& splines;std::shared_ptr<ParticleRandom> random;bool released_handler;
  struct Particle {legacy::sVector position,speed,rotation,rotation_speed;};
  struct Cloud {std::array<std::uint32_t,16> key{};std::vector<Particle> particles;bool initialized{};};
  std::map<std::size_t,Cloud> clouds;std::size_t bytes{};
  Impl(const SplineLibrary& s,std::shared_ptr<ParticleRandom> rng,bool released):splines(s),random(rng ? std::move(rng) : std::make_shared<ParticleRandom>()),released_handler(released) {}
  const Cloud& cloud(std::size_t id,const std::array<std::uint32_t,30>& p) {
    // RandSpeed is stored but not compared by the source. It is used from the
    // current parameters during motion, so changing it does not reseed a cloud.
    std::array<std::uint32_t,16> key{};unsigned n=0;
    for(auto index:{0u,1u,2u,3u,4u,5u,6u,7u,8u,22u,23u,24u,25u,26u})
      if(!released_handler || index<6)key[n++]=p[index];
    auto& cached=clouds[id];if(cached.initialized && cached.key==key)return cached;
    const auto new_bytes=std::size_t(p[1])*sizeof(Particle),old_bytes=cached.particles.size()*sizeof(Particle);
    require(new_bytes<=64u*1024*1024-(bytes-old_bytes),"scene particle cache budget exceeded");
    Cloud next;next.key=key;next.particles.reserve(p[1]);auto& rng=*random;rng.seed(p[2]);const unsigned mode=p[0];
    for(unsigned i=0;i<p[1];++i) {
      Particle v{};
      if((mode&12)==8)v.position=rng.sphere();
      else {v.position=rng.vector();if((mode&12)==4)v.position.Unit3();}
      v.position.w=(mode&0x20) ? float(i)/p[1] : rng.real(1-as_float(p[23]))+as_float(p[23]);
      for(unsigned c=0;c<3;++c)v.position[c]*=as_float(p[3+c])*.5f;
      // Final 1.01's 0x8206ed handler stores only position and phase. Later
      // archive code draws additional random values even for zero velocities.
      if(!released_handler) {
        v.speed=rng.sphere();v.speed.w=rng.real();
        if(mode&0x10) {
          for(unsigned c=0;c<3;++c)v.rotation[c]=rng.real(as_float(p[6+c]));
          for(unsigned c=0;c<3;++c)v.rotation_speed[c]=rng.real(as_float(p[24+c]));
        }
      }
      next.particles.push_back(v);
    }
    next.initialized=true;bytes=bytes-old_bytes+new_bytes;cached=std::move(next);return cached;
  }
};
SceneParticles::SceneParticles(const SplineLibrary& s,std::shared_ptr<ParticleRandom> rng,bool released):impl(std::make_unique<Impl>(s,std::move(rng),released)) {}
SceneParticles::~SceneParticles()=default;
std::vector<SceneParticle> SceneParticles::evaluate(std::size_t id,std::span<const std::uint32_t> parameters,std::optional<std::size_t> spline) {
  const Nearest rounding;
  // WerkDoc::Read copies the saved prefix onto the editor defaults. The added
  // parameters 16..29 all default to zero; Theta saves the older 16-word layout.
  require(parameters.size()>=16 && parameters.size()<=30,"unsupported scene Particles parameter layout");
  const bool released=impl->released_handler;
  if(released)require(parameters.size()==16 && !(parameters[0]&~0x3fu),"unsupported released Particles layout/flags");
  std::array<std::uint32_t,30> p{};std::copy(parameters.begin(),parameters.end(),p.begin());
  const auto mode=p[0];require(!(mode&~0xffu),"optimized particle mesh instances are not ported");require(p[1]<=16384,"scene particle count limit");
  for(unsigned i=3;i<30;++i)require(std::isfinite(as_float(p[i])) && std::abs(as_float(p[i]))<=1e6f,"invalid scene particle numeric parameter");
  require(as_float(p[23])>=0 && as_float(p[23])<=1,"invalid particle come-out range");
  const auto& cloud=impl->cloud(id,p);float animation=as_float(p[12]);
  if(released || !(mode&0x80))animation=std::fmod(double(animation),1.0);
  if(animation<0)animation+=1;
  std::vector<SceneParticle> result;result.reserve(cloud.particles.size());
  for(unsigned i=0;i<cloud.particles.size();++i) {
    const auto& v=cloud.particles[i];float fraction=animation+v.position.w+as_float(p[22])*v.speed.w*animation;
    require(std::isfinite(fraction) && std::abs(fraction)<16777216,"particle phase exceeds exact wrap range");
    if(fraction<1 && (mode&0x40))continue;
    // Within this float range repeated subtraction of 1 has the same result;
    // negative fractions remain negative, matching the source's while(f>=1).
    if(fraction>=1)fraction-=std::floor(fraction);
    legacy::sMatrix m;m.Init();const float squared=fraction*fraction;legacy::sVector position;
    for(unsigned c=0;c<3;++c)position[c]=v.position[c]+v.speed[c]*fraction*as_float(p[16])+squared*as_float(p[19+c]);
    // The source uses RandSpeed.x on all three axes.
    if(as_float(p[29])!=0)position.Scale3(legacy::sFSin(animation*legacy::sPI2F*as_float(p[27])+fraction*legacy::sPI2F*as_float(p[28]))*as_float(p[29])+1);
    if((mode&1) && spline) {
      const auto value=impl->splines.evaluate(*spline,fraction);
      if(mode&0x10) {
        const auto next=impl->splines.evaluate(*spline,fraction+.01f);
        m.j.Init3(0,1,0);m.k.Init3(next[0]-value[0],next[1]-value[1],next[2]-value[2]);m.k.UnitSafe3();
        m.i.Cross3(m.j,m.k);m.i.UnitSafe3();m.j.Cross3(m.k,m.i);
      }
      for(unsigned c=0;c<3;++c)m.l[c]+=position[c]+value[c];
    }
    // In final 1.01, spline and line placement are exclusive. The later
    // archive handler's else belongs to a separate GenSpline input branch.
    if(!released || !(mode&1) || !spline)
      for(unsigned c=0;c<3;++c)m.l[c]+=position[c]+as_float(p[13+c])*fraction;
    if((mode&0x10) && !released) {
      legacy::sMatrix rotation,combined;std::array<float,3> angles{};
      for(unsigned c=0;c<3;++c)angles[c]=fraction*as_float(p[9+c])+v.rotation[c]+v.rotation_speed[c]*fraction;
      rotation.InitEuler(angles[0],angles[1],angles[2]);combined.MulA(rotation,m);m=combined;
    }
    const auto transform=matrix(m);for(float value:transform)require(std::isfinite(value),"nonfinite particle transform");
    result.push_back({i,fraction,transform});
  }
  return result;
}
} // namespace wz3
