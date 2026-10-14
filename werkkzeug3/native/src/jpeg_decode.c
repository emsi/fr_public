// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
// Keep libjpeg's longjmp error handling entirely inside C, so it cannot bypass
// C++ destructors. Mutable decoder state lives on the heap across setjmp.
#include "jpeg_decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <jpeglib.h>

struct error_state {
  struct jpeg_error_mgr base;
  jmp_buf jump;
  char* message;
  size_t capacity;
};
struct decoder {
  struct jpeg_decompress_struct jpeg;
  struct error_state error;
  unsigned char* pixels;
};
static void fail(j_common_ptr jpeg) {
  struct error_state* error = (struct error_state*)jpeg->err;
  char message[JMSG_LENGTH_MAX];
  (*jpeg->err->format_message)(jpeg, message);
  snprintf(error->message, error->capacity, "%s", message);
  longjmp(error->jump, 1);
}
static void warning(j_common_ptr jpeg, int level) {
  if (level < 0) fail(jpeg); // Do not silently synthesize pixels for truncated data.
}
int wz3_decode_jpeg(const unsigned char* bytes, size_t size, struct wz3_jpeg_image* output,
                    char* error, size_t error_size) {
  struct decoder* const decoder = calloc(1, sizeof(*decoder));
  *output = (struct wz3_jpeg_image){0, 0, NULL};
  if (!decoder) { snprintf(error, error_size, "cannot allocate JPEG decoder"); return 0; }
  decoder->jpeg.err = jpeg_std_error(&decoder->error.base);
  decoder->error.base.error_exit = fail;
  decoder->error.base.emit_message = warning;
  decoder->error.message = error;
  decoder->error.capacity = error_size;
  if (setjmp(decoder->error.jump)) {
    jpeg_destroy_decompress(&decoder->jpeg);
    free(decoder->pixels);
    free(decoder);
    return 0;
  }
  jpeg_create_decompress(&decoder->jpeg);
  decoder->jpeg.mem->max_memory_to_use = 64 * 1024 * 1024;
  jpeg_mem_src(&decoder->jpeg, bytes, size);
  jpeg_read_header(&decoder->jpeg, TRUE);
  if (!decoder->jpeg.image_width || !decoder->jpeg.image_height ||
      decoder->jpeg.image_width > 4096 || decoder->jpeg.image_height > 4096) {
    snprintf(error, error_size, "JPEG dimensions exceed 4096 limit");
    longjmp(decoder->error.jump, 1);
  }
  decoder->jpeg.out_color_space = JCS_RGB;
  jpeg_start_decompress(&decoder->jpeg);
  const size_t stride = (size_t)decoder->jpeg.output_width * 3;
  decoder->pixels = malloc(stride * decoder->jpeg.output_height);
  if (!decoder->pixels) {
    snprintf(error, error_size, "cannot allocate JPEG pixels");
    longjmp(decoder->error.jump, 1);
  }
  while (decoder->jpeg.output_scanline < decoder->jpeg.output_height) {
    JSAMPROW row = decoder->pixels + stride * decoder->jpeg.output_scanline;
    jpeg_read_scanlines(&decoder->jpeg, &row, 1);
  }
  jpeg_finish_decompress(&decoder->jpeg);
  output->width = decoder->jpeg.output_width;
  output->height = decoder->jpeg.output_height;
  output->rgb = decoder->pixels;
  jpeg_destroy_decompress(&decoder->jpeg);
  free(decoder);
  return 1;
}
