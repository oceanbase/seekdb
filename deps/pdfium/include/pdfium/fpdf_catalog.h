// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PUBLIC_FPDF_CATALOG_H_
#define PUBLIC_FPDF_CATALOG_H_

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Experimental API.
//
// Determine if |document| represents a tagged PDF.
//
// For the definition of tagged PDF, See (see 10.7 "Tagged PDF" in PDF
// Reference 1.7).
//
//   document - handle to a document.
//
// Returns |true| iff |document| is a tagged PDF.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDFCatalog_IsTagged(FPDF_DOCUMENT document);

// Experimental API.
// Gets the language of |document| from the catalog's /Lang entry.
//
//   document - handle to a document.
//   buffer   - a buffer for the language string. May be NULL.
//   buflen   - the length of the buffer, in bytes. May be 0.
//
// Returns the number of bytes in the language string, including the
// trailing NUL character. The number of bytes is returned regardless of the
// |buffer| and |buflen| parameters.
//
// Regardless of the platform, the |buffer| is always in UTF-16LE
// encoding. The string is terminated by a UTF16 NUL character. If
// |buflen| is less than the required length, or |buffer| is NULL,
// |buffer| will not be modified.
//
// If |document| has no /Lang entry, an empty string is written to |buffer| and
// 2 is returned. On error, nothing is written to |buffer| and 0 is returned.
FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFCatalog_GetLanguage(FPDF_DOCUMENT document,
                        FPDF_WCHAR* buffer,
                        unsigned long buflen);

// Experimental API.
// Sets the language of |document| to |language|.
//
// document - handle to a document.
// language - the language to set to.
//
// Returns TRUE on success.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDFCatalog_SetLanguage(FPDF_DOCUMENT document, FPDF_WIDESTRING language);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_FPDF_CATALOG_H_
