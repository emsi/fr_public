// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#include "wz3/texture.hpp"
#include <list>
#include <stdexcept>
namespace wz3 {
struct TextureCache::Impl {
  const Project& project;const Graph& graph;
  std::filesystem::path font;const RenderedBitmapProvider* rendered;
  std::size_t limit;Stats stats;std::vector<bool> reusable;
  std::list<std::size_t> recent;
  struct Entry {std::shared_ptr<const Texture> texture;std::list<std::size_t>::iterator position;};
  std::map<std::size_t,Entry> entries;
  Impl(const Project& p,const Graph& g,const TextureOptions& o,std::size_t bytes)
    :project(p),graph(g),font(o.font_file),rendered(o.rendered_bitmaps.get()),limit(bytes),reusable(p.operators.size()) {
    if(!bytes || bytes>1024u*1024*1024 || g.nodes.size()!=p.operators.size())throw std::runtime_error("invalid shared texture cache configuration");
    std::vector<std::size_t> consumers(g.nodes.size());
    for(const auto& node:g.nodes)if(node.active) {
      for(auto input:node.inputs)if(input<consumers.size())++consumers[input];
      for(auto link:node.links)if(link && *link<consumers.size())++consumers[*link];
    }
    for(std::size_t id=0;id<reusable.size();++id) {
      const auto command=p.operators[id].command;
      reusable[id]=command==2 || (command!=1 && command!=3 && consumers[id]>1);
    }
  }
};
TextureCache::TextureCache(const Project& p,const Graph& g,const TextureOptions& o,std::size_t bytes):impl(std::make_unique<Impl>(p,g,o,bytes)){}
TextureCache::~TextureCache()=default;
bool TextureCache::matches(const Project& p,const Graph& g,const TextureOptions& o) const {
  return &p==&impl->project && &g==&impl->graph && o.font_file==impl->font && o.rendered_bitmaps.get()==impl->rendered;
}
std::shared_ptr<const Texture> TextureCache::find(std::size_t id) {
  auto& s=*impl;if(id>=s.reusable.size() || !s.reusable[id])return {};
  const auto found=s.entries.find(id);
  if(found==s.entries.end()){++s.stats.misses;return {};}
  s.recent.splice(s.recent.begin(),s.recent,found->second.position);++s.stats.hits;return found->second.texture;
}
void TextureCache::remember(std::size_t id,const Texture& texture) {
  auto& s=*impl;if(id>=s.reusable.size() || !s.reusable[id] || s.entries.contains(id))return;
  const auto bytes=texture.pixels.size()*sizeof(Pixel);if(bytes>s.limit)return;
  while(bytes>s.limit-s.stats.pixel_bytes) {
    const auto old=s.recent.back();s.stats.pixel_bytes-=s.entries.at(old).texture->pixels.size()*sizeof(Pixel);
    s.entries.erase(old);s.recent.pop_back();++s.stats.evictions;
  }
  auto copy=std::make_shared<Texture>(texture);
  s.recent.push_front(id);
  try{s.entries.emplace(id,Impl::Entry{std::move(copy),s.recent.begin()});}catch(...){s.recent.pop_front();throw;}
  s.stats.pixel_bytes+=bytes;
}
TextureCache::Stats TextureCache::stats() const {auto out=impl->stats;out.textures=impl->entries.size();return out;}
}
