// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Arithmetic ported from NewBitmap, GetColor64, Fade64, Bitmap_Flat,
// Bitmap_Gradient and Bitmap_GlowRect in ../genbitmap.cpp.
#include "wz3/texture.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>

namespace wz3 {
Pixel color64(std::uint32_t argb) {
  Pixel pixel{};
  constexpr unsigned shifts[] = {16, 8, 0, 24};
  for (unsigned i = 0; i < 4; ++i) pixel[i] = (((argb >> shifts[i]) & 255) * 257) >> 1;
  return pixel;
}

Pixel fade64(const Pixel& a, const Pixel& b, std::int32_t fade) {
  // Original MMX: signed saturated weights, pmulhw for each term, then
  // paddw and psllw. Rounding each product separately is intentional.
  const auto half = std::int64_t(fade) >> 1;
  const auto w1 = std::clamp<std::int64_t>(half, -32768, 32767);
  const auto w0 = std::clamp<std::int64_t>(32768 - half, -32768, 32767);
  Pixel result{};
  for (unsigned i = 0; i < 4; ++i)
    result[i] = static_cast<std::uint16_t>(((std::bit_cast<std::int16_t>(a[i]) * w0 >> 16) +
                                          (std::bit_cast<std::int16_t>(b[i]) * w1 >> 16)) * 2);
  return result;
}

namespace {
// The original sPI2 and trig helpers are double precision. sFtol then
// receives a float; preserve that boundary instead of using float sin/cos.
constexpr double pi2 = 2.0 * std::numbers::pi_v<double>;
std::int32_t rounded(float value) {
  // sFloatFix sets x87 to round-to-nearest-even before the old sFtol.
  if (!std::isfinite(value) || std::abs(double(value)) > 2147483647.0)
    throw std::runtime_error("texture arithmetic exceeds int32 range");
  return static_cast<std::int32_t>(std::nearbyint(value));
}
int clamp15(double value) {
  if (!std::isfinite(value)) throw std::runtime_error("non-finite texture arithmetic");
  return static_cast<int>(std::clamp(value, 0.0, 32767.0));
}

class Evaluator {
public:
  Evaluator(const Project& project, const Graph& graph)
      : project_(project), graph_(graph), cache_(project.operators.size()), visiting_(cache_.size()) {}

  std::shared_ptr<const Texture> evaluate(std::size_t id, unsigned depth = 0) {
    if (id >= cache_.size() || !graph_.nodes.at(id).active) throw std::runtime_error("invalid or inactive texture operator");
    if (cache_[id]) return cache_[id];
    if (visiting_[id] || depth > 512) throw std::runtime_error("cycle or excessive depth in texture graph");
    visiting_[id] = true;
    const auto& op = project_.operators[id];
    const auto& node = graph_.nodes[id];
    if (!op.animations.empty()) throw std::runtime_error("animated texture operators are not implemented");
    const auto input = [&]() {
      if (node.inputs.size() != 1) throw std::runtime_error("texture operator requires exactly one input");
      return evaluate(node.inputs.front(), depth + 1);
    };
    if (op.command == 1) {
      if (node.links.size() != 1 || !node.links[0]) throw std::runtime_error("texture Load has no resolved link");
      cache_[id] = evaluate(*node.links[0], depth + 1);
    } else if (op.command == 2 || op.command == 3) {
      cache_[id] = input();
    } else if (op.command == 0x21 || op.command == 0x34) {
      if (!node.inputs.empty()) throw std::runtime_error("texture generator has unexpected inputs");
      require(op, op.command == 0x21 ? 3 : 8);
      const auto width = 1u << std::clamp<int>(op.parameters[0] & 0x7f, 0, 12);
      const auto height = 1u << std::clamp(std::bit_cast<std::int32_t>(op.parameters[1]), 0, 12);
      auto texture = allocate(width, height);
      if (op.command == 0x21) std::fill(texture->pixels.begin(), texture->pixels.end(), color64(op.parameters[2]));
      else gradient(*texture, op);
      cache_[id] = std::move(texture);
    } else if (op.command == 0x24) {
      require(op, 1);
      if (node.inputs.empty()) throw std::runtime_error("Merge requires at least one input");
      auto texture = clone(*evaluate(node.inputs[0], depth + 1));
      for (std::size_t i = 1; i < node.inputs.size(); ++i) {
        const auto source = evaluate(node.inputs[i], depth + 1);
        if (texture->width != source->width || texture->height != source->height)
          throw std::runtime_error("Merge inputs have different dimensions");
        for (std::size_t p = 0; p < texture->pixels.size(); ++p)
          texture->pixels[p] = merge_pixel(texture->pixels[p], source->pixels[p], op.parameters[0]);
      }
      cache_[id] = std::move(texture);
    } else if (op.command == 0x23 || op.command == 0x25 || op.command == 0x28) {
      require(op, op.command == 0x23 ? 2 : op.command == 0x25 ? 3 : 4);
      auto texture = clone(*input());
      if (op.command == 0x23) {
        const auto color = color64(op.parameters[1]);
        for (auto& pixel : texture->pixels) pixel = color_pixel(pixel, color, op.parameters[0]);
      } else if (op.command == 0x25) {
        if (op.parameters[0] == 0) throw std::runtime_error("invalid texture format");
        texture->format = op.parameters[0];
        texture->mip_count = op.parameters[1];
        texture->mip_threshold = op.parameters[2];
      } else draw_dots(*texture, op.parameters[0], op.parameters[1], op.parameters[2], op.parameters[3]);
      cache_[id] = std::move(texture);
    } else if (op.command == 0x2c) {
      require(op, 8);
      const auto source = input();
      if (op.parameters[6] > 13 || op.parameters[7] > 13)
        throw std::runtime_error("transform output dimensions exceed limit");
      const auto width = op.parameters[6] ? 1u << (op.parameters[6] - 1) : source->width;
      const auto height = op.parameters[7] ? 1u << (op.parameters[7] - 1) : source->height;
      auto texture = allocate(width, height);
      metadata(*texture, *source);
      transform_bitmap(*source, *texture, number(op, 0), number(op, 1), number(op, 2),
                       number(op, 3), number(op, 4), op.parameters[5]);
      cache_[id] = std::move(texture);
    } else if (op.command == 0x29 || op.command == 0x2b || op.command == 0x2e) {
      require(op, op.command == 0x2e ? 2 : 4);
      const auto source = input();
      auto texture = clone(*source);
      if (op.command == 0x29) {
        auto scratch = allocate(source->width, source->height);
        blur_bitmap(*texture, *scratch, op.parameters[0], number(op, 1), number(op, 2), number(op, 3));
      } else if (op.command == 0x2b) {
        adjust_hscb(*texture, number(op, 0), number(op, 1), number(op, 2), number(op, 3));
      } else normal_bitmap(*source, *texture, number(op, 0), op.parameters[1]);
      cache_[id] = std::move(texture);
    } else if (op.command == 0x36) {
      require(op, 10);
      cache_[id] = transform_mul(*input(), op);
    } else if (op.command == 0x39) {
      require(op, 11);
      const auto base = input();
      auto texture = clone(*base);
      glow(*texture, number(op, 0), number(op, 1), number(op, 2), number(op, 3),
           number(op, 4), number(op, 5), op.parameters[6], number(op, 7), number(op, 8),
           op.parameters[9], op.parameters[10], 0);
      cache_[id] = std::move(texture);
    } else {
      throw std::runtime_error("operator " + std::to_string(id) + " (" +
          std::string(operator_info(op.command).name) + ", command " + std::to_string(op.command) + ") is not ported");
    }
    visiting_[id] = false;
    return cache_[id];
  }
private:
  const Project& project_;
  const Graph& graph_;
  std::vector<std::shared_ptr<const Texture>> cache_;
  std::vector<bool> visiting_;
  std::shared_ptr<std::size_t> allocated_ = std::make_shared<std::size_t>(0);
  static void require(const Operator& op, std::size_t count) {
    if (op.parameters.size() != count) throw std::runtime_error("unsupported texture parameter layout");
  }
  static float number(const Operator& op, std::size_t index) {
    const auto value = as_float(op.parameters.at(index));
    if (!std::isfinite(value) || std::abs(value) > 1000000)
      throw std::runtime_error("invalid texture parameter");
    return value;
  }
  std::shared_ptr<Texture> allocate(unsigned width, unsigned height) {
    const auto size = std::size_t(width) * height;
    if (size * sizeof(Pixel) > 256 * 1024 * 1024 - *allocated_)
      throw std::runtime_error("texture cache exceeds 256 MiB limit");
    auto texture = std::make_unique<Texture>();
    texture->width = width; texture->height = height; texture->pixels.resize(size);
    *allocated_ += size * sizeof(Pixel);
    return std::shared_ptr<Texture>(texture.release(), [budget = allocated_, size](Texture* object) {
      delete object;
      *budget -= size * sizeof(Pixel);
    });
  }
  std::shared_ptr<Texture> clone(const Texture& source) {
    auto result = allocate(source.width, source.height);
    *result = source;
    return result;
  }
  static void metadata(Texture& dest, const Texture& source) {
    dest.format = source.format; dest.mip_count = source.mip_count; dest.mip_threshold = source.mip_threshold;
  }
  std::shared_ptr<Texture> transform_mul(const Texture& source, const Operator& op) {
    auto angle = number(op, 0), sx = number(op, 1), sy = number(op, 2);
    auto tx = number(op, 3), ty = number(op, 4);
    const auto border = op.parameters[5], mode = op.parameters[7], count = op.parameters[8];
    auto fade = op.parameters[9];
    if (!count || count > 512 || (mode & 15) > 10 || mode > 31 || ((mode & 16) && count > 16))
      throw std::runtime_error("invalid TransformMul mode/count");
    auto base = clone(source);
    const auto color = color64(op.parameters[6]);
    for (auto& p : base->pixels) p = color_pixel(p, color, 0);
    auto result = clone(*base);
    auto scratch = allocate(source.width, source.height);
    const auto divisor = mode & 16 ? (1u << count) - 1 : count;
    angle /= mode & 16 ? 1u << count : count;
    tx = (tx - 0.5) / divisor + 0.5f; ty = (ty - 0.5) / divisor + 0.5f;
    sx = std::pow(double(std::abs(sx)), double(1.0f / divisor)) * (sx < 0 ? -1 : 1);
    sy = std::pow(double(std::abs(sy)), double(1.0f / divisor)) * (sy < 0 ? -1 : 1);
    for (unsigned i = 1; i <= count; ++i) {
      if (fade != 0xffffffff) {
        const auto color_fade = color64(fade);
        for (auto& p : base->pixels) p = color_pixel(p, color_fade, 0);
      }
      if (mode & 16) {
        if (fade != 0xffffffff) {
          std::uint32_t next = 0;
          for (unsigned shift = 0; shift < 32; shift += 8) {
            const auto channel = (fade >> shift) & 255;
            next |= ((channel * channel) >> 8) << shift;
          }
          fade = next;
        }
        transform_bitmap(*base, *scratch, angle, sx, sy, tx, ty, border);
        for (std::size_t p = 0; p < result->pixels.size(); ++p)
          result->pixels[p] = merge_pixel(scratch->pixels[p], result->pixels[p], mode & 15);
        base->pixels = result->pixels;
        angle *= 2; tx = (tx - 0.5f) * 2 + 0.5f; ty = (ty - 0.5f) * 2 + 0.5f;
        sx *= sx; sy *= sy;
      } else {
        transform_bitmap(*base, *scratch, angle * i, (sx - 1) * i + 1, (sy - 1) * i + 1,
                         (tx - 0.5) * i + 0.5, (ty - 0.5) * i + 0.5, border);
        for (std::size_t p = 0; p < result->pixels.size(); ++p)
          result->pixels[p] = merge_pixel(result->pixels[p], scratch->pixels[p], mode & 15);
      }
    }
    return result;
  }
  static void gradient(Texture& texture, const Operator& op) {
    const auto a = color64(op.parameters[2]), b = color64(op.parameters[3]);
    const auto position = number(op, 4), angle = number(op, 5), length = number(op, 6);
    if (length == 0 || op.parameters[7] > 2) throw std::runtime_error("invalid gradient length/mode");
    const float scale = 32768.0f / length;
    const auto dx = rounded(std::cos(angle * pi2) * scale);
    const auto dy = rounded(std::sin(angle * pi2) * scale);
    const std::int64_t step_x = (std::int64_t(dx) * (65536 / texture.width)) >> 16;
    const std::int64_t step_y = (std::int64_t(dy) * (65536 / texture.height)) >> 16;
    const double initial = 16384.0f - (std::int64_t(dx) / 2 + std::int64_t(dy) / 2) * (position + 1);
    if (!std::isfinite(initial) || std::abs(initial) > 2147483647)
      throw std::runtime_error("gradient initial value exceeds int32 range");
    const auto start = static_cast<std::int64_t>(initial); // original implicit truncation
    for (unsigned y = 0; y < texture.height; ++y) {
      for (unsigned x = 0; x < texture.width; ++x) {
        const auto value = std::clamp<std::int64_t>(start + y * step_y + x * step_x, 0, 32767);
        int fade = static_cast<int>(value * 2);
        if (op.parameters[7] == 1) fade = rounded(std::sin(value * pi2 / 32768 + 8192) * 32767 + 32767);
        if (op.parameters[7] == 2) fade = rounded(std::sin(value * pi2 / 65536) * 65535);
        texture.pixels[std::size_t(y) * texture.width + x] = fade64(a, b, fade);
      }
    }
  }
  static void glow(Texture& texture, float cx, float cy, float rx, float ry, float sx, float sy,
                   std::uint32_t color, float alpha, float power, unsigned wrap, unsigned bug, unsigned depth) {
    if (depth > 3 || wrap > 2 || power < 0) throw std::runtime_error("invalid GlowRect parameters");
    if (wrap == 1) {
      if (cx + rx + sx > 1) glow(texture, cx-1, cy, rx, ry, sx, sy, color, alpha, power, 2, bug, depth+1);
      if (cx - rx - sx < 0) glow(texture, cx+1, cy, rx, ry, sx, sy, color, alpha, power, 2, bug, depth+1);
    }
    if (wrap == 1 || wrap == 2) {
      if (cy + ry + sy > 1) glow(texture, cx, cy-1, rx, ry, sx, sy, color, alpha, power, 0, bug, depth+1);
      if (cy - ry - sy < 0) glow(texture, cx, cy+1, rx, ry, sx, sy, color, alpha, power, 0, bug, depth+1);
    }
    const bool circular = !(bug & 2);
    power = 0.25f / (power == 0 ? 1.0f / 65536 : power);
    cx *= texture.width; rx *= texture.width; sx *= texture.width;
    cy *= texture.height; ry *= texture.height; sy *= texture.height;
    rx = std::max(rx, 1.0f / 65536); ry = std::max(ry, 1.0f / 65536);
    rx = circular ? 1.0f / (rx * rx) : 1.0f / rx;
    ry = circular ? 1.0f / (ry * ry) : 1.0f / ry;
    alpha *= 32768;
    std::array<int, 1025> table{};
    std::array<int, 32> low{};
    const auto curve = [&](float x) {
      return clamp15((bug & 1 ? std::pow(1.0 - x, double(power)) :
          1.0 - std::pow(double(x), double(power * 2))) * alpha) * 2;
    };
    for (std::size_t i = 0; i < table.size(); ++i) table[i] = curve(float(i) / 1024);
    for (std::size_t i = 0; i < low.size(); ++i) low[i] = curve(float(i) / 32768);
    const auto overlay = color64(color);
    for (unsigned y = 0; y < texture.height; ++y) {
      float fy = std::max(0.0f, std::abs(float(y) - cy) - sy);
      fy *= circular ? fy * ry : ry;
      for (unsigned x = 0; x < texture.width; ++x) {
        const float fx = std::max(0.0f, std::abs(float(x) - cx) - sx);
        const float distance = circular ? fx * fx * rx + fy : std::max(fx * rx, fy);
        if (distance < 1.0f - 1.0f / 32768) {
          const auto index = rounded(distance * 32768);
          if (index < 0 || index >= 32768) throw std::runtime_error("GlowRect lookup out of range");
          const auto fade = index < 32 ? low[index] : table[index >> 5] +
              (((table[(index >> 5) + 1] - table[index >> 5]) * (index & 31)) >> 5);
          auto& pixel = texture.pixels[std::size_t(y) * texture.width + x];
          pixel = fade64(pixel, overlay, fade);
        }
      }
    }
  }
};
}

std::shared_ptr<const Texture> evaluate_texture(const Project& project, const Graph& graph, std::size_t id) {
  return Evaluator(project, graph).evaluate(id);
}

std::vector<std::uint8_t> rgba8(const Texture& texture) {
  std::vector<std::uint8_t> result;
  result.reserve(texture.pixels.size() * 4);
  for (const auto& pixel : texture.pixels)
    for (auto channel : pixel) result.push_back(static_cast<std::uint8_t>(std::min<unsigned>(channel >> 7, 255)));
  return result;
}

void write_ppm(const std::filesystem::path& path, unsigned width, unsigned height, std::span<const std::uint8_t> rgba) {
  if (rgba.size() != std::size_t(width) * height * 4) throw std::runtime_error("invalid output pixel count");
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("cannot open output: " + path.string());
  out << "P6\n" << width << ' ' << height << "\n255\n";
  for (std::size_t i = 0; i < rgba.size(); i += 4) out.write(reinterpret_cast<const char*>(rgba.data() + i), 3);
  if (!out) throw std::runtime_error("cannot write output: " + path.string());
}
} // namespace wz3
