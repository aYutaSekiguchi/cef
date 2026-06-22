# QNX exclude pdfium_print from //pdf

## Summary

`enable_pdf=true` on QNX so the `//pdf` component builds, but
`enable_printing=false` so the `//printing` component is not built. The
`pdfium/pdfium_print.cc` source is unconditionally included in the
`//pdf` source list and references `printing::NupParameters`,
`printing::PageSetup::GetSymmetricalPrintableArea`, and
`printing::ConvertUnit*`, producing undefined references during
`libcef.so` link.

## Patches

```text
cef/patch/patches/qnx/chromium/pdf_pdfium_print_qnx_exclude.patch
cef/patch/qnx/chromium/new_files/pdf/pdfium/pdfium_print_qnx.cc
cef/patch/qnx/chromium/new_files/pdf/pdfium/pdfium_printing_units_qnx.cc
```

## Details

Keep `pdfium/pdfium_print.h` visible because `PDFiumEngine` owns a `PDFiumPrint`
member, but replace upstream `pdfium/pdfium_print.cc` with QNX no-op
implementations. Also provide QNX-local `printing::ConvertUnit*` definitions for
the PDF target because `printing/units.cc` lives under the disabled
`//printing` build file.

The companion change to `cef/BUILD.gn` excludes CEF's
`print_settings_impl.{cc,h}` from `libcef_static` for QNX so the CEF
public `CefPrintSettings` API resolves to a no-op when the CEF print
backend is disabled.

## Verification

```bash
autoninja -C out/qnx_release cefsimple
```

Observed `GN_EXIT:0`; the `printing/pdfium undefined refs:` grep was empty. The wider `cefsimple` link still fails on unrelated platform/link groups (payments, views, syncer, platform utilities, on_device, crashpad, etc.).

## Search hints

```bash
rg -n "NupParameters::NupParameters|PageSetup::GetSymmetricalPrintableArea|ConvertUnit\(|pdfium_print" docs/qnx/history/build-errors
```
