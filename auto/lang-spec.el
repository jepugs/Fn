(TeX-add-style-hook
 "lang-spec"
 (lambda ()
   (TeX-add-to-alist 'LaTeX-provided-class-options
                     '(("article" "11pt")))
   (TeX-add-to-alist 'LaTeX-provided-package-options
                     '(("inputenc" "utf8") ("fontenc" "T1") ("ulem" "normalem")))
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "href")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "hyperref")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "hyperimage")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "hyperbaseurl")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "nolinkurl")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "url")
   (add-to-list 'LaTeX-verbatim-macros-with-braces-local "path")
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
    "sec:orge8c6f6a"
    "sec:orgbc0d4fa"
    "sec:org91300c7"
    "sec:orga508ab5"
    "sec:orgc302283"
    "sec:org25d2c11"
    "sec:orgd17d65e"
    "sec:org10b3e3a"
    "sec:orga48c356"
    "sec:org4014214"
    "sec:orgdf14b97"
    "sec:org67814c5"
    "sec:org0a9ff87"
    "sec:org87d2bc1"
    "sec:org8352aea"
    "sec:org9945995"
    "sec:org975cdcb"
    "sec:org4b0fe4d"
    "sec:orgdeda7f2"
    "sec:org85057e2"
    "sec:orgb5d06fc"
    "sec:org3673a57"
    "sec:org725e1c4"
    "sec:org73c1c41"
    "sec:orgb06d932"
    "sec:orgf78a814"
    "sec:org24ca7ef"
    "sec:org66b2d79"
    "sec:orgccc45ce"
    "sec:orgc6e15b7"
    "sec:orgcf965a5"
    "sec:orgd2a8583"
    "sec:orgcbb20c2"
    "sec:org7c526d7"
    "sec:org04e81c1"
    "sec:orga9c498c"
    "sec:orgd4f17ad"
    "sec:org6e1a6d5"
    "sec:org4e4e8bd"
    "sec:org4dec73f"
    "sec:org670e3b5"
    "sec:org0edb310"
    "sec:org86dee5a"
    "sec:orgc57dccf"
    "sec:org94bce3a"
    "sec:orgf21b880"
    "sec:orgcd4124b"
    "sec:orgfe08fb0"
    "sec:org3e26911"
    "sec:orgdce4a8b"
    "sec:org3b2c38d"
    "sec:orgb336eed"
    "sec:orga472829"
    "sec:orgaaae3dc"
    "sec:org08d4324"
    "sec:org4232e10"
    "sec:org01888e7"
    "sec:org523c108"
    "sec:org9c50f32"
    "sec:org4e804c2"
    "sec:orgf3599f1"
    "sec:org72dcd46"
    "sec:orgb8d083e"
    "sec:org6d1883e"
    "sec:org9fae995"
    "sec:orgc8beef5"
    "sec:org223188a"
    "sec:org34618cf"
    "sec:orge599d70"))
 :latex)

