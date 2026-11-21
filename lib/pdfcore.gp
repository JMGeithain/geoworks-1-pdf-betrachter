# SPDX-License-Identifier: MIT
name pdfcore.lib
type library, single
longname "PDF Kern"
tokenchars "PDFK"
tokenid 0

# Import-free: file access goes through a far callback supplied by the app.
# Calls run on the caller's stack; the library never uses DS.
resource ALGORITHM fixed code shared read-only
resource PDFTABS fixed read-only shared
export _pdf_open
export _pdf_page_count
export _pdf_page_size
export _pdf_render
export _pdf_errtext
export _pdf_set_ext
