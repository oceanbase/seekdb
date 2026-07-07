/**
 * Copyright (c) 2025 OceanBase
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OCEANBASE_LIB_AI_PARSE_DOC_OB_PDF_RENDER_H_
#define OCEANBASE_LIB_AI_PARSE_DOC_OB_PDF_RENDER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_iarray.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace common
{

// Renders a PDF (given as raw bytes) to per-page PNG images and returns each as a
// "data:image/png;base64,..." URL, ready to drop into an OpenAI-compatible vision
// message. Backed by pdfium (page raster) + stb_image_write (PNG encode).
class ObPdfRender
{
public:
  // - pdf_data/pdf_len: the raw PDF bytes.
  // - scale:            raster scale (1.0 == 72dpi); <= 0 defaults to 2.0 (~144dpi).
  // - max_pages:        cap on rendered pages; <= 0 means render all pages.
  // - allocator:        owns every returned ObString (a request-scoped arena is expected).
  // - out_data_urls:    appended with one data URL per rendered page.
  // pdfium's internal allocations honour whatever ObMallocHookAttrGuard the caller holds.
  static int render_to_png_base64_urls(const char *pdf_data, int64_t pdf_len,
                                       double scale, int64_t max_pages,
                                       ObIAllocator &allocator,
                                       ObIArray<ObString> &out_data_urls);
};

}  // namespace common
}  // namespace oceanbase

#endif  // OCEANBASE_LIB_AI_PARSE_DOC_OB_PDF_RENDER_H_
