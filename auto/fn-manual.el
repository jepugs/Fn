(TeX-add-style-hook
 "fn-manual"
 (lambda ()
   (TeX-add-to-alist 'LaTeX-provided-class-options
                     '(("article" "11pt")))
   (TeX-add-to-alist 'LaTeX-provided-package-options
                     '(("inputenc" "utf8") ("fontenc" "T1") ("ulem" "normalem")))
   (add-to-list 'LaTeX-verbatim-environments-local "minted")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "path")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "url")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "nolinkurl")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "hyperbaseurl")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "hyperimage")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "hyperref")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "href")
   (add-to-list 'LaTeX-verbatim-macros-with-delims-local "path")
   (TeX-run-style-hooks
    "latex2e"
    "article"
    "art11"
    "inputenc"
    "fontenc"
    "graphicx"
    "grffile"
    "longtable"
    "wrapfig"
    "rotating"
    "ulem"
    "amsmath"
    "textcomp"
    "amssymb"
    "capt-of"
    "hyperref")
   (LaTeX-add-labels
    "sec:orgd42282c"
    "sec:orgabc4d33"
    "sec:orgab6441a"
    "sec:orgd32069b"
    "sec:org2d3f66e"
    "sec:org761ce8a"
    "sec:org45e7d44"
    "sec:org51a388d"
    "sec:org6918fb9"
    "sec:orgd68ab38"
    "sec:org73f773f"
    "sec:org85de6b3"
    "sec:orgef30853"
    "sec:org679c46d"
    "sec:orgb1b9cca"
    "sec:orgd6347d1"
    "sec:orgc5c7f45"
    "sec:org5f73b62"
    "sec:org8e5416c"
    "sec:org3eb67b4"
    "sec:org8fdf53d"
    "sec:orgf0cd9c9"
    "sec:org532b7e4"
    "sec:org3370284"
    "sec:orgb090073"
    "sec:orgbcf79c6"
    "sec:org20cc8cc"
    "sec:orgf5b9097"
    "sec:org76c455d"))
 :latex)

