// Copyright 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "pdf/pdfium/pdfium_print.h"

#include "pdf/pdfium/pdfium_engine.h"
#include "third_party/blink/public/web/web_print_params.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace chrome_pdf {

PDFiumPrint::PDFiumPrint(PDFiumEngine* engine) : engine_(engine) {}

PDFiumPrint::~PDFiumPrint() = default;

// static
std::vector<uint8_t> PDFiumPrint::CreateNupPdf(
    ScopedFPDFDocument doc,
    size_t pages_per_sheet,
    const gfx::Size& page_size,
    const gfx::Rect& printable_area) {
  return std::vector<uint8_t>();
}

// static
bool PDFiumPrint::IsSourcePdfLandscape(FPDF_DOCUMENT doc) {
  return false;
}

// static
void PDFiumPrint::FitContentsToPrintableArea(FPDF_DOCUMENT doc,
                                             const gfx::Size& page_size,
                                             const gfx::Rect& printable_area) {}

std::vector<uint8_t> PDFiumPrint::PrintPagesAsPdf(
    base::span<const int> page_indices,
    const blink::WebPrintParams& print_params) {
  return std::vector<uint8_t>();
}

ScopedFPDFDocument PDFiumPrint::CreatePrintPdf(
    base::span<const int> page_indices,
    const blink::WebPrintParams& print_params) {
  return nullptr;
}

ScopedFPDFDocument PDFiumPrint::CreateRasterPdf(ScopedFPDFDocument doc,
                                                int dpi) {
  return nullptr;
}

ScopedFPDFDocument PDFiumPrint::CreateSinglePageRasterPdf(FPDF_PAGE page_to_print,
                                                          int dpi) {
  return nullptr;
}

}  // namespace chrome_pdf
