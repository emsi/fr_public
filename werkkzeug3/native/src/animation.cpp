// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// References: WerkOp::ConnectAnim/GenCodeAnim, AnimOpCmds in winpara.cpp,
// KEnvironment::ExecuteAnim, KSplineChannel::Eval and KOp::ExecEvent.
#include "wz3/animation.hpp"
#include "noise.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <functional>
#include <numeric>
#include <stdexcept>

namespace wz3 {
namespace {
class NearestRounding {
  std::fenv_t previous;
public:
  NearestRounding() {
    if(std::fegetenv(&previous)!=0)throw std::runtime_error("cannot save animation floating-point state");
    if(std::fesetround(FE_TONEAREST)!=0) {std::fesetenv(&previous);throw std::runtime_error("cannot set animation rounding");}
  }
  ~NearestRounding() {std::fesetenv(&previous);}
};
void require(bool valid,const char* message) {if(!valid)throw std::runtime_error(message);}
float finite(std::uint32_t bits) {
  const auto value=as_float(bits);require(std::isfinite(value),"nonfinite animation constant");return value;
}
std::int32_t signed_word(std::uint32_t value) {return std::bit_cast<std::int32_t>(value);}
std::int32_t truncate(float value) {
  // Win32 __ftol2 uses a 64-bit conversion, returning the low word for int32.
  require(std::isfinite(value) && double(value)>=-0x1p63 && double(value)<0x1p63,"animation integer conversion overflow");
  return signed_word(static_cast<std::uint32_t>(static_cast<std::int64_t>(value)));
}
unsigned family(unsigned cmd) {return cmd>=0x80 ? cmd&0xf0 : cmd;}
bool is_store(unsigned cmd) {
  const auto f=family(cmd);return f==0x80 || f==0x90 || f==0xa0 || f==0xb0 || f==0xd0 || f==0xe0 || f==0xf0;
}
bool is_parameter(unsigned cmd) {return is_store(cmd) && family(cmd)!=0x80;}
unsigned inputs(unsigned cmd) {
  if(cmd==2 || (cmd>=3 && cmd<=6) || (cmd>=0x16 && cmd<=0x18))return 0;
  if((cmd>=0x0b && cmd<=0x0f) || cmd==0x1f)return 2;
  require(cmd<=0x21 || is_store(cmd),"unknown animation opcode");return 1;
}
unsigned union_kind(unsigned cmd) {
  if(is_store(cmd))return 5;
  if(cmd>=0x16 && cmd<=0x19)return cmd-0x15;
  return cmd==0x1d ? 6 : cmd==0x1e ? 7 : 0;
}
float curve_value(const std::vector<std::array<float,2>>& keys,float time,unsigned mode) {
  if(keys.empty())return 0;
  if(time<=keys.front()[0])return keys.front()[1];
  if(time>=keys.back()[0])return keys.back()[1];
  for(std::size_t k=0;k+1<keys.size();++k)if(keys[k+1][0]>time) {
    const auto dif=keys[k+1][1]-keys[k][1],tdif=keys[k+1][0]-keys[k][0];
    const auto t=(time-keys[k][0])/tdif;
    if(mode==1)return t*dif+keys[k][1];
    if(mode==2)return keys[k][1];
    const auto d0=k==0 ? dif : tdif*(keys[k+1][1]-keys[k-1][1])/(keys[k+1][0]-keys[k-1][0]);
    const auto d1=k==keys.size()-2 ? dif : tdif*(keys[k+2][1]-keys[k][1])/(keys[k+2][0]-keys[k][0]);
    return (((d0+d1-2*dif)*t+3*dif-2*d0-d1)*t+d0)*t+keys[k][1];
  }
  return 0;
}
struct Reader {
  std::span<const std::uint8_t> code;std::size_t at{};
  unsigned byte() {require(at<code.size(),"truncated animation bytecode");return code[at++];}
  std::uint32_t word() {std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=byte()<<(8*i);return v;}
  float real() {return finite(word());}
};
}

SplineLibrary::SplineLibrary(const Project& project) {
  for(const auto& source:project.splines) {
    require(source.channels.size()<=4 && source.interpolation<=2,"unsupported spline layout");
    names.emplace(source.name,curves.size()); // FindSpline selects the first match, including duplicate names.
    Curve curve{source.interpolation,{}};
    for(const auto& channel:source.channels) {
      auto& keys=curve.channels.emplace_back();
      for(const auto& key:channel)keys.push_back({finite(key[0]),finite(key[1])});
      const auto ordered=[&](){return std::is_sorted(keys.begin(),keys.end(),[](const auto& a,const auto& b){return a[0]<b[0];});};
      if(!ordered()) {
        // Preserve the editor's exact sorting passes and equal-key order.
        require(keys.size()<=4096,"unordered spline sorting limit");
        for(std::size_t i=0;i+1<keys.size();++i)for(std::size_t j=i;j+1<keys.size();++j)
          if(keys[j][0]>keys[j+1][0])std::swap(keys[j],keys[j+1]);
        require(ordered(),"spline keys remain unordered after legacy sorting");
      }
    }
    curves.push_back(std::move(curve));
  }
}
std::optional<std::size_t> SplineLibrary::find(std::string_view name) const {
  const auto it=names.find(name);if(it==names.end())return {};return it->second;
}
AnimationVector SplineLibrary::evaluate(std::size_t id,float time) const {
  require(id<curves.size() && std::isfinite(time),"invalid spline sample");
  const NearestRounding rounding;AnimationVector value{};const auto& curve=curves[id];
  for(std::size_t i=0;i<curve.channels.size();++i)value[i]=curve_value(curve.channels[i],time,curve.interpolation);
  return value;
}

namespace {
AnimationProgram runtime_animation(const Operator& op,const SplineLibrary& splines) {
  require(op.animations.empty(),"operator mixes editor animation and runtime bytecode");
  require(op.animation_code.size()<=4096,"runtime animation bytecode limit");
  AnimationProgram program;program.code=op.animation_code;Reader r{program.code};
  unsigned stack=0,stores=0;
  const auto push=[&](){require(stack<255,"runtime animation stack overflow");++stack;};
  const auto pop=[&](){require(stack>0,"runtime animation stack underflow");--stack;};
  for(;;) {
    const auto cmd=r.byte(),group=family(cmd);
    if(cmd==1) {require(stack==0 && r.at==program.code.size(),"unbalanced or trailing runtime bytecode");break;}
    if(cmd==0)continue;
    if(is_store(cmd)) {
      const auto index=r.byte();pop();
      if(group==0x80) {
        require(index<32 && stores++<256,"runtime animation variable store limit");program.written_variables|=1u<<index;
      }else {
        require(index<op.parameters.size(),"runtime parameter store index");
        if(group!=0xb0 && group!=0xf0)for(unsigned c=0;c<4;++c)if(cmd&(1u<<c))
          require(index+c<op.parameters.size(),"runtime parameter store overrun");
      }
    }else if(cmd==2) {
      const auto index=r.byte();require(index<32,"runtime animation variable index");program.read_variables|=1u<<index;push();
    }else if(cmd>=3 && cmd<=6) {
      const auto index=r.byte();require(index+cmd-2<=op.parameters.size(),"runtime parameter load overrun");push();
    }else if(cmd>=0x16 && cmd<=0x18) {
      if(cmd==0x16)for(unsigned i=0;i<4;++i)r.real();
      else if(cmd==0x17)r.word();else r.real();
      push();
    }else if((cmd>=0x0b && cmd<=0x0f) || cmd==0x1f) {pop();pop();push();}
    else {
      require(cmd<=0x21 && stack>0,"unknown opcode or runtime animation stack underflow");
      if(cmd==0x19) {const auto low=r.byte();const auto index=low|(r.byte()<<8);require(index<splines.size(),"runtime animation spline index");}
      else if(cmd==0x1d){r.real();r.real();}
      else if(cmd==0x1e){r.byte();r.byte();}
    }
  }
  return program;
}
}
AnimationProgram compile_animation(const Operator& op,const SplineLibrary& splines) {
  if(!op.animation_code.empty())return runtime_animation(op,splines);
  const NearestRounding rounding;
  const auto n=op.animations.size();require(n<=4096,"animation node limit");
  AnimationProgram program;program.source_nodes=n;
  std::vector<std::size_t> order(n);std::iota(order.begin(),order.end(),0);
  const auto coordinate=[&](std::size_t i,unsigned c){return std::int64_t(signed_word(op.animations[i].fields[c]));};
  std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return coordinate(a,1)<coordinate(b,1);});
  std::vector<std::vector<std::size_t>> links(n);std::vector<unsigned> outputs(n);
  std::vector<bool> used(n),invalid(n);
  for(auto i:order) {
    const auto width=coordinate(i,2);invalid[i]=width<=0 || width>65536;
    for(auto j:order)if(coordinate(j,1)==coordinate(i,1)-1 && coordinate(j,0)<coordinate(i,0)+width && coordinate(j,0)+coordinate(j,2)>coordinate(i,0)) {
      links[i].push_back(j);++outputs[j];
    }
    // The source uses pairwise swaps, which matter for equal-X inputs.
    for(std::size_t j=0;j<links[i].size();++j)for(std::size_t k=j+1;k<links[i].size();++k)
      if(coordinate(links[i][j],0)>coordinate(links[i][k],0))std::swap(links[i][j],links[i][k]);
  }
  for(std::size_t i=0;i<n;++i) {
    const auto cmd=op.animations[i].fields[3];require(cmd<=255,"invalid animation opcode byte");
    const auto wanted=inputs(cmd);
    invalid[i]=invalid[i] || (links[i].size()!=wanted && !(wanted==0 && links[i].size()==1)) ||
      (is_store(cmd) ? outputs[i]!=0 : outputs[i]==0);
    if(cmd==0x19 && !splines.find(op.animations[i].name))invalid[i]=true;
  }
  std::array<unsigned,32> stored_vars{};std::vector<unsigned> stored_params(op.parameters.size());
  const auto byte=[&](unsigned value){require(program.code.size()<4095,"animation bytecode limit");program.code.push_back(value);};
  const auto word=[&](std::uint32_t value){for(unsigned i=0;i<4;++i)byte((value>>(8*i))&255);};
  std::function<void(std::size_t,unsigned)> emit=[&](std::size_t id,unsigned depth) {
    require(depth<256 && !invalid[id],"invalid connected animation node");
    const auto& a=op.animations[id];const auto cmd=a.fields[3],param=a.fields[4],mask=cmd&15;
    require(a.fields[5]==union_kind(cmd),"animation union kind does not match opcode");
    used[id]=true;
    for(auto input:links[id])emit(input,depth+1);
    if(family(cmd)==0x80 || cmd==2) {
      require(param<32,"animation variable index");
      if(cmd==2) {require(!stored_vars[param],"animation reads variable after storing it");program.read_variables|=1u<<param;}
      else {require(!(stored_vars[param]&mask),"overlapping animation variable stores");stored_vars[param]|=mask;program.written_variables|=1u<<param;}
    }
    if(is_parameter(cmd) || (cmd>=3 && cmd<=6)) {
      require(param<op.parameters.size() && param<256,"animation parameter index");
      if(is_parameter(cmd)) {
        require(param<64,"legacy animation parameter store index");
        require(!(stored_params[param]&mask),"overlapping animation parameter stores");stored_params[param]|=mask;
        if(family(cmd)!=0xb0 && family(cmd)!=0xf0)for(unsigned c=0;c<4;++c)
          if(mask&(1u<<c))require(param+c<op.parameters.size(),"animation parameter store overrun");
      }else require(param+cmd-3<op.parameters.size(),"animation parameter load overrun");
    }
    byte(cmd);
    if(is_store(cmd) || (cmd>=2 && cmd<=6))byte(param);
    const auto kind=union_kind(cmd);
    const unsigned count=kind==1 ? 4 : (kind==2 || kind==3) ? 1 : (kind==6 || kind==7) ? 2 : 0;
    require(a.values.size()==count,"invalid animation constant layout");
    if(kind==4) {
      const auto spline=*splines.find(a.name);require(spline<=65535,"animation spline index");byte(spline&255);byte(spline>>8);
    }else if(kind==7) {
      for(auto value:a.values)byte(std::clamp(truncate(finite(value)),0,255));
    }else for(auto value:a.values) {if(kind!=2)finite(value);word(value);}
    if(inputs(cmd)==0 && links[id].size()==1) {
      constexpr unsigned automatic[]={0x0d,0x0b,0x0c,0x0e};require(a.fields[6]<4,"invalid animation automatic operation");byte(automatic[a.fields[6]]);
    }
  };
  for(auto id:order)if(is_store(op.animations[id].fields[3]) && !links[id].empty())emit(id,0);
  byte(1);program.used_nodes=std::count(used.begin(),used.end(),true);return program;
}

AnimationEnvironment frame_environment(std::int32_t beat) {
  const NearestRounding rounding;AnimationEnvironment env;env.variables[0].fill(beat/65536.0f);env.variables[4]={1,1,1,0};return env;
}
void AnimationScope::restore() noexcept {
  for(auto it=saved.rbegin();it!=saved.rend();++it)environment.variables[it->first]=it->second;
  saved.clear();
}
AnimationScope::~AnimationScope() {restore();}
AnimationScope::AnimationScope(const AnimationProgram& program,const SplineLibrary& splines,
    std::span<const std::uint32_t> original,AnimationEnvironment& env,std::span<const std::uint32_t> previous):environment(env) {
  const NearestRounding rounding;
  require(previous.empty() || previous.size()==original.size(),"animation previous parameter size mismatch");
  auto initial=previous.empty() ? original : previous;parameters.assign(initial.begin(),initial.end());
  require(program.code.size()<=4096,"animation bytecode limit");
  Reader reader{program.code};std::vector<AnimationVector> stack;stack.reserve(256);
  const auto push=[&](AnimationVector v){require(stack.size()<255,"animation stack overflow");stack.push_back(v);};
  const auto take=[&](){require(!stack.empty(),"animation stack underflow");auto v=stack.back();stack.pop_back();return v;};
  try {
    for(;;) {
      const auto cmd=reader.byte(),group=family(cmd);
      if(cmd==1) {require(stack.empty() && reader.at==program.code.size(),"unbalanced or trailing animation bytecode");break;}
      if(is_store(cmd)) {
        const auto index=reader.byte();const auto value=take();
        if(group==0x80) {
          require(index<32 && saved.size()<256,"animation variable store limit");
          saved.emplace_back(index,env.variables[index]);
          for(unsigned c=0;c<4;++c)if(cmd&(1u<<c))env.variables[index][c]=value[c];
        }else {
          require(index<parameters.size(),"animation parameter store index");bool cp=false;
          for(unsigned c=0;c<4;++c)if(cmd&(1u<<c)) {
            if(group==0xb0 || group==0xf0) {
              const auto v=unsigned(std::clamp(truncate(value[c]*255),0,255)),shift=8*c;
              cp|=((parameters[index]>>shift)&255)!=v;
              parameters[index]=(parameters[index]&~(255u<<shift))|(v<<shift);
            }else {
              require(index+c<parameters.size(),"animation parameter store overrun");
              if(group==0x90 || group==0xd0) {cp|=as_float(parameters[index+c])!=value[c];parameters[index+c]=std::bit_cast<std::uint32_t>(value[c]);}
              else {cp|=float(signed_word(parameters[index+c]))!=value[c];parameters[index+c]=std::uint32_t(truncate(value[c]));}
            }
          }
          if(group>=0xd0)changed|=cp;
        }
        continue;
      }
      if(cmd==2) {const auto index=reader.byte();require(index<32,"animation variable load index");push(env.variables[index]);continue;}
      if(cmd>=3 && cmd<=6) {
        const auto index=reader.byte(),count=cmd-2;require(index+count<=original.size(),"animation parameter load overrun");
        AnimationVector value{};for(unsigned c=0;c<count;++c)value[c]=as_float(original[index+c]);
        if(count==1)value.fill(value[0]);
        push(value);continue;
      }
      if(cmd>=0x16 && cmd<=0x18) {
        AnimationVector value{};
        if(cmd==0x16)for(auto& v:value)v=reader.real();
        else if(cmd==0x17)for(auto& v:value)v=reader.byte()/255.0f; // Original little-endian BGRA lane order.
        else value.fill(reader.real());
        push(value);continue;
      }
      if((cmd>=0x0b && cmd<=0x0f) || cmd==0x1f) {
        const auto b=take();auto a=take();
        for(unsigned c=0;c<4;++c)switch(cmd) {
          case 0x0b:a[c]+=b[c];break;case 0x0c:a[c]-=b[c];break;case 0x0d:a[c]*=b[c];break;
          case 0x0e:a[c]/=b[c];break;case 0x0f:a[c]=std::fmod(double(a[c]),double(b[c]));break;
          case 0x1f:a[c]=std::pow(double(a[c]),double(b[c]));break;
        }
        push(a);continue;
      }
      if(cmd==0)continue;
      require(!stack.empty(),"animation stack underflow");auto& a=stack.back();
      if(cmd>=7 && cmd<=10) {a.fill(a[cmd-7]);continue;}
      switch(cmd) {
      case 0x10:for(auto& v:a)v=1-v;break;
      case 0x11:for(auto& v:a)v=-v;break;
      case 0x12:case 0x13: {
        const float phase=a[0]*6.28318530717958647692f;
        a.fill(cmd==0x12 ? std::sin(double(phase)) : std::cos(double(phase)));break;
      }
      case 0x14:case 0x15:case 0x1c: {
        float phase=std::fmod(double(a[0]),1.0);if(phase<0)phase+=1;
        if(cmd==0x14)phase=phase<.5f ? 1 : 0;
        if(cmd==0x1c) {
          const auto v=std::uint32_t(truncate(phase*65536));const auto& permutation=legacy::perlin_permutation();
          phase=(permutation[(v>>8)&255]|(unsigned(permutation[v&255])<<8))/65535.0f;
        }
        a.fill(phase);break;
      }
      case 0x19:case 0x1a: {
        std::optional<std::size_t> id=env.event_spline;
        if(cmd==0x19) {auto low=reader.byte();id=low|(reader.byte()<<8);}
        a=id ? splines.evaluate(*id,a[0]) : AnimationVector{};break;
      }
      case 0x1b: {
        auto v=a;for(unsigned c=0;c<4;++c)a[c]=env.variables[28][c]*v[0]+env.variables[29][c]*v[1]+env.variables[30][c]*v[2]+env.variables[31][c]*v[3];break;
      }
      case 0x1d: {
        auto ei=std::min(reader.real(),.5f),eo=std::min(reader.real(),.5f);eo=1-eo;auto d=eo-ei;
        if(d<.1) {eo+=(.1-d)/2;ei-=(.1-d)/2;d=eo-ei;}const float s=1/d;
        for(auto& v:a) {
          if(v<0)v=-ei*s/2;
          else if(v<ei)v=-ei*s/2+v*v*(s/(2*ei));
          else if(v<eo)v=(v-ei)*s;
          else if(v<1)v=1+(1-eo)*s/2-(1-v)*(1-v)*(s/(2*(1-eo)));
          else v=1+(1-eo)*s/2;
          v=(v+ei*s/2)/(1+ei*s/2+(1-eo)*s/2);
        }break;
      }
      case 0x1e: {
        const auto t0=reader.byte()/256.0f,t1=reader.byte()/256.0f;
        for(auto& v:a)v=v<=t0 ? 0 : v>=t1 ? 1 : (v-t0)/(t1-t0);
        break;
      }
      // Preserve the archive's formula, despite its historical "log2" label.
      case 0x20:for(auto& v:a)v=std::log(double(v))*.69314718055994530941723212145818f;break;
      case 0x21:for(auto& v:a)v=std::pow(2.0,double(v));break;
      default:throw std::runtime_error("unknown animation bytecode");
      }
    }
  }catch(...) {restore();throw;}
}

bool event_active(const Event& event,std::int32_t beat) {
  const auto start=signed_word(event.fields[1]),end=signed_word(event.fields[2]);
  return !event.fields[10] && (start==end || (beat>=start && beat<end));
}
EventAnimationScope::EventAnimationScope(const Event& event,std::int32_t beat,const SplineLibrary& splines,AnimationEnvironment& env,std::optional<std::size_t> event_id):
    environment(env),saved_spline(env.event_spline),saved_instances(env.instances) {
  const NearestRounding rounding;std::copy_n(env.variables.begin(),8,saved.begin());
  const auto start=signed_word(event.fields[1]),end=signed_word(event.fields[2]);
  if(start<end)time=float(std::int64_t(beat)-start)/float(std::int64_t(end)-start);
  time=std::clamp(time,0.0f,1.0f);time=finite(event.interval[0])+time*(finite(event.interval[1])-finite(event.interval[0]));
  auto values=saved;
  if(!(event.flags&1))values[0].fill(time);
  values[1].fill(finite(event.fields[4]));values[2].fill(finite(event.fields[5]));values[3].fill(float(signed_word(event.fields[3])));
  // The .k project reader uses sVector::Read3, which initializes W to one
  // for all three SRT variables, including rotation and scale.
  for(unsigned i=0;i<3;++i) {values[4+i]={0,0,0,1};for(unsigned c=0;c<3;++c)values[4+i][c]=finite(event.transform[3*i+c]);}
  for(unsigned c=0;c<4;++c)values[7][c]=((event.fields[9]>>(c==0 ? 16 : c==1 ? 8 : c==2 ? 0 : 24))&255)/255.0f;
  // A missing event spline leaves an enclosing event's spline in place.
  const auto spline=splines.find(event.spline);if(spline)env.event_spline=spline;
  std::copy(values.begin(),values.end(),env.variables.begin());
  if(event_id)env.instances=std::make_shared<AnimationInstanceCursor>(AnimationInstanceCursor{*event_id+1,0});
}
EventAnimationScope::~EventAnimationScope() {
  std::copy(saved.begin(),saved.end(),environment.variables.begin());environment.event_spline=saved_spline;environment.instances=saved_instances;
}
} // namespace wz3
