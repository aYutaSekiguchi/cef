// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "cef/include/cef_print_settings.h"

namespace {

class CefPrintSettingsQnx final : public CefPrintSettings {
 public:
  CefPrintSettingsQnx() = default;

  bool IsValid() override { return true; }
  bool IsReadOnly() override { return false; }

  void SetOrientation(bool landscape) override { landscape_ = landscape; }
  bool IsLandscape() override { return landscape_; }

  void SetPrinterPrintableArea(const CefSize& physical_size_device_units,
                               const CefRect& printable_area_device_units,
                               bool landscape_needs_flip) override {
    physical_size_device_units_ = physical_size_device_units;
    printable_area_device_units_ = printable_area_device_units;
    landscape_needs_flip_ = landscape_needs_flip;
  }

  void SetDeviceName(const CefString& name) override { device_name_ = name; }
  CefString GetDeviceName() override { return device_name_; }

  void SetDPI(int dpi) override { dpi_ = dpi; }
  int GetDPI() override { return dpi_; }

  void SetPageRanges(const PageRangeList& ranges) override { ranges_ = ranges; }
  size_t GetPageRangesCount() override { return ranges_.size(); }
  void GetPageRanges(PageRangeList& ranges) override { ranges = ranges_; }

  void SetSelectionOnly(bool selection_only) override {
    selection_only_ = selection_only;
  }
  bool IsSelectionOnly() override { return selection_only_; }

  void SetCollate(bool collate) override { collate_ = collate; }
  bool WillCollate() override { return collate_; }

  void SetColorModel(ColorModel model) override { color_model_ = model; }
  ColorModel GetColorModel() override { return color_model_; }

  void SetCopies(int copies) override { copies_ = copies; }
  int GetCopies() override { return copies_; }

  void SetDuplexMode(DuplexMode mode) override { duplex_mode_ = mode; }
  DuplexMode GetDuplexMode() override { return duplex_mode_; }

 private:
  bool landscape_ = false;
  CefSize physical_size_device_units_;
  CefRect printable_area_device_units_;
  bool landscape_needs_flip_ = false;
  CefString device_name_;
  int dpi_ = 0;
  PageRangeList ranges_;
  bool selection_only_ = false;
  bool collate_ = false;
  ColorModel color_model_ = COLOR_MODEL_UNKNOWN;
  int copies_ = 0;
  DuplexMode duplex_mode_ = DUPLEX_MODE_UNKNOWN;

  IMPLEMENT_REFCOUNTING(CefPrintSettingsQnx);
};

}  // namespace

// static
CefRefPtr<CefPrintSettings> CefPrintSettings::Create() {
  return new CefPrintSettingsQnx();
}
