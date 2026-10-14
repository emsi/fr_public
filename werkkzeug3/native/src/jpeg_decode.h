// Distributed under the BSD license in werkkzeug3/LICENSE.txt.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
struct wz3_jpeg_image { unsigned width, height; unsigned char* rgb; };
// The caller owns rgb (free()). Failure leaves the output empty.
int wz3_decode_jpeg(const unsigned char* bytes, size_t size, struct wz3_jpeg_image* output,
                    char* error, size_t error_size);
#ifdef __cplusplus
}
#endif
