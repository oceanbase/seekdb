/**
 * Copyright (c) 2025 OceanBase
 * SPDX-License-Identifier: Apache-2.0
 */

#define USING_LOG_PREFIX COMMON

#include "lib/ai_parse_doc/ob_pdf_render.h"
#include "fpdfview.h"
#include "lib/encode/ob_base64_encode.h"
#include "lib/oblog/ob_log.h"
#include <pthread.h>

// stb's single-header implementation trips several of the build's -Werror
// warnings (missing field initializers, etc.); scope the suppression to it.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

namespace oceanbase
{
namespace common
{

// FPDF_InitLibrary() is not re-entrant and only needs to run once per process.
// We never call FPDF_DestroyLibrary() -- pdfium's global state lives for the
// process lifetime, which avoids tearing it down under concurrent callers.
static pthread_once_t g_pdfium_once = PTHREAD_ONCE_INIT;
static void ob_pdfium_do_init() { FPDF_InitLibrary(); }

// Growable byte buffer backed by an arena allocator. stb hands PNG data over in
// chunks via a callback; we accumulate them. Growing allocs a bigger block and
// copies -- the old block stays in the arena until the request allocator resets.
struct ObPngSink
{
  explicit ObPngSink(ObIAllocator &alloc)
      : alloc_(alloc), buf_(nullptr), len_(0), cap_(0), failed_(false)
  {}
  void append(const void *data, int64_t n)
  {
    if (failed_ || n <= 0) {
      // nothing
    } else {
      if (len_ + n > cap_) {
        int64_t ncap = (cap_ <= 0) ? (n > (64L << 10) ? n : (64L << 10)) : cap_;
        while (ncap < len_ + n) { ncap <<= 1; }
        char *nb = static_cast<char *>(alloc_.alloc(ncap));
        if (OB_ISNULL(nb)) {
          failed_ = true;
        } else {
          if (OB_NOT_NULL(buf_) && len_ > 0) { MEMCPY(nb, buf_, len_); }
          buf_ = nb;
          cap_ = ncap;
        }
      }
      if (!failed_) {
        MEMCPY(buf_ + len_, data, n);
        len_ += n;
      }
    }
  }
  ObIAllocator &alloc_;
  char *buf_;
  int64_t len_;
  int64_t cap_;
  bool failed_;
};

static void ob_pdf_png_write_cb(void *context, void *data, int size)
{
  static_cast<ObPngSink *>(context)->append(data, size);
}

// PNG bytes -> "data:image/png;base64,...." ObString (allocator-owned), appended to out.
static int encode_png_as_data_url(ObIAllocator &allocator, const char *png, int64_t png_len,
                                  ObIArray<ObString> &out)
{
  int ret = OB_SUCCESS;
  static const char *const PREFIX = "data:image/png;base64,";
  const int64_t PREFIX_LEN = 22;  // strlen(PREFIX)
  if (OB_ISNULL(png) || png_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty png buffer", K(ret), K(png_len));
  } else {
    const int64_t b64_cap = ObBase64Encoder::needed_encoded_length(png_len);
    const int64_t total = PREFIX_LEN + b64_cap;
    char *dst = static_cast<char *>(allocator.alloc(total));
    int64_t pos = 0;
    if (OB_ISNULL(dst)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc data url", K(ret), K(total));
    } else {
      MEMCPY(dst, PREFIX, PREFIX_LEN);
      if (OB_FAIL(ObBase64Encoder::encode(reinterpret_cast<const uint8_t *>(png), png_len,
                                          dst + PREFIX_LEN, b64_cap, pos))) {
        LOG_WARN("failed to base64 encode png", K(ret), K(png_len));
      } else if (OB_FAIL(out.push_back(ObString(PREFIX_LEN + pos, dst)))) {
        LOG_WARN("failed to append data url", K(ret));
      }
    }
  }
  return ret;
}

static int render_one_page(FPDF_DOCUMENT doc, int index, double scale, int max_dim,
                           ObIAllocator &allocator, ObIArray<ObString> &out)
{
  int ret = OB_SUCCESS;
  FPDF_PAGE page = FPDF_LoadPage(doc, index);
  if (OB_ISNULL(page)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to load pdf page", K(ret), K(index));
  } else {
    const double wd = FPDF_GetPageWidth(page);
    const double ht = FPDF_GetPageHeight(page);
    int W = static_cast<int>(wd * scale);
    int H = static_cast<int>(ht * scale);
    if (W < 1) { W = 1; }
    if (H < 1) { H = 1; }
    if (W > max_dim) { W = max_dim; }
    if (H > max_dim) { H = max_dim; }
    // format 0 == FPDFBitmap_BGRx: 4 opaque bytes per pixel.
    FPDF_BITMAP bmp = FPDFBitmap_Create(W, H, 0);
    char *rgb = nullptr;
    if (OB_ISNULL(bmp)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to create pdf bitmap", K(ret), K(W), K(H));
    } else {
      FPDFBitmap_FillRect(bmp, 0, 0, W, H, 0xFFFFFFFF);
      FPDF_RenderPageBitmap(bmp, page, 0, 0, W, H, 0, FPDF_ANNOT);
      if (OB_ISNULL(rgb = static_cast<char *>(
                        allocator.alloc(static_cast<int64_t>(W) * H * 3)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc rgb buffer", K(ret), K(W), K(H));
      } else {
        const unsigned char *src = static_cast<const unsigned char *>(FPDFBitmap_GetBuffer(bmp));
        const int stride = FPDFBitmap_GetStride(bmp);
        // BGRx -> RGB
        for (int y = 0; y < H; ++y) {
          const unsigned char *row = src + static_cast<int64_t>(y) * stride;
          char *orow = rgb + static_cast<int64_t>(y) * W * 3;
          for (int x = 0; x < W; ++x) {
            const unsigned char *p = row + static_cast<int64_t>(x) * 4;
            char *o = orow + static_cast<int64_t>(x) * 3;
            o[0] = static_cast<char>(p[2]);
            o[1] = static_cast<char>(p[1]);
            o[2] = static_cast<char>(p[0]);
          }
        }
        ObPngSink sink(allocator);
        const int ok = stbi_write_png_to_func(ob_pdf_png_write_cb, &sink, W, H, 3, rgb, W * 3);
        if (0 == ok || sink.failed_ || sink.len_ <= 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to encode page png", K(ret), K(ok), K(sink.failed_), K(sink.len_));
        } else if (OB_FAIL(encode_png_as_data_url(allocator, sink.buf_, sink.len_, out))) {
          LOG_WARN("failed to encode page data url", K(ret), K(index));
        }
      }
    }
    if (OB_NOT_NULL(bmp)) { FPDFBitmap_Destroy(bmp); }
    FPDF_ClosePage(page);
  }
  return ret;
}

int ObPdfRender::render_to_png_base64_urls(const char *pdf_data, int64_t pdf_len,
                                           double scale, int64_t max_pages,
                                           ObIAllocator &allocator,
                                           ObIArray<ObString> &out_data_urls)
{
  int ret = OB_SUCCESS;
  const int MAX_DIM = 4000;  // guard against pathologically large pages
  out_data_urls.reset();
  if (OB_ISNULL(pdf_data) || pdf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty pdf content", K(ret), K(pdf_len));
  } else {
    (void)pthread_once(&g_pdfium_once, ob_pdfium_do_init);
    if (scale <= 0.0) { scale = 2.0; }
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(pdf_data, static_cast<int>(pdf_len), nullptr);
    if (OB_ISNULL(doc)) {
      ret = OB_INVALID_ARGUMENT;
      const unsigned long err = FPDF_GetLastError();
      LOG_WARN("failed to load pdf document (not a valid PDF?)", K(ret), K(err), K(pdf_len));
    } else {
      const int page_count = FPDF_GetPageCount(doc);
      int limit = page_count;
      if (max_pages > 0 && max_pages < static_cast<int64_t>(limit)) {
        limit = static_cast<int>(max_pages);
      }
      for (int i = 0; OB_SUCC(ret) && i < limit; ++i) {
        if (OB_FAIL(render_one_page(doc, i, scale, MAX_DIM, allocator, out_data_urls))) {
          LOG_WARN("failed to render pdf page", K(ret), K(i), K(page_count));
        }
      }
      FPDF_CloseDocument(doc);
      if (OB_SUCC(ret) && out_data_urls.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("pdf produced no renderable pages", K(ret), K(page_count));
      }
    }
  }
  return ret;
}

}  // namespace common
}  // namespace oceanbase
