// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include "mesh_kernel.hpp"
namespace wz3::legacy {
struct sQuaternion {
  float w{},x{},y{},z{};
  void Init(float ww,float xx,float yy,float zz) {w=ww;x=xx;y=yy;z=zz;}
  void Scale(float v) {w*=v;x*=v;y*=v;z*=v;}
  void Unit();
  void Lin(const sQuaternion&,const sQuaternion&,float);
  void ToMatrix(sMatrix&) const;
  void FromMatrix(const sMatrix&);
};
struct BlobSplineKey {
  int Select{};
  float Time{},px{},py{},pz{},rx{},ry{},rz{},Zoom=1;
};
static_assert(sizeof(BlobSplineKey)==36);
struct BlobSpline {
  int Count{},Version=3,Select=-1,Mode{},Uniform{};
  sVector Target;
  float Continuity{},Tension{};
  std::vector<BlobSplineKey> Keys;
  void Sort();
  float Normalize();
  void Calc(float,sMatrix&,float&);
};
void sPerlin3D(const sVector&,sVector&);
} // namespace wz3::legacy
