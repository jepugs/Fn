(provide 'fn-mode)

(defvar fn-mode-map (make-sparse-keymap))

(defcustom fn-mode-hook '()
  "List of functions to call after `fn-mode' is enabled."
  :type 'hook
  :options nil)

(add-to-list 'auto-mode-alist '("\\.fn\\'" . fn-mode))
(add-to-list 'auto-mode-alist '("\\.Fn\\'" . fn-mode))

(setq completion-ignored-extensions
      (remove ".fn" completion-ignored-extensions))

;;; Indentation Code

;; Command names that might trigger repeated indentation
(defvar fn-mode--manual-indent-commands
  '(fn-mode-indent-line indent-for-tab-command indent-according-to-mode))

;; FIXME: right now, we don't consider comment locations for indentation. Should
;; try that.
(defun fn-mode-indent-line (&optional arg)
  "Indent the current line as Fn code. Issuing the command
repeatedly will cycle through several valid indentation levels
when applicable (to better support user macros)."
  (interactive "*P")
  (let ((offsets (fn-mode--compute-indents))
        (repeated (and (not arg)
                       (eq last-command this-command)
                       (memq last-command fn-mode--manual-indent-commands)))
        (col (save-excursion
               (back-to-indentation)
               (current-column)))
        (start (point)))
    (cond
     ;; prefix arg with already valid indent
     ((and arg (memq col offsets)) nil)
     ;; repeated means we have to find the next offsets
     (repeated
      (let ((x (cdr (memq col offsets))))
        (fn-mode--do-indent (if x (car x) (car offsets)))))
     (t (if (memq col offsets)
            ;; gotta do this to make sure the point ends up after the
            ;; indentation (even if we don't indent)
            (if (< (current-column) col)
                (back-to-indentation))
          (fn-mode--do-indent (car offsets)))))))

(defun fn-mode--do-indent (column)
  (let (start (point))
    (save-excursion
      (back-to-indentation)
      (delete-backward-char (- (point) (line-beginning-position)))
      (indent-to column)))
  (when (< (current-column) column)
    (back-to-indentation)))

;; indentation: Go to the first column. Then:
;;; - inside a string => do nothing
;;; - at top level => indent to 0
;;; - in [] or {} => indent one past the opening delimiter
;;; - in (), in operator position => toggle between 1- and 2- past delimiter
;;; - in (), after operator position => different cases
;;;   - operator is special => indent according to operator
;;;   - in 2nd position => toggle between 1- and 2-past
;;;   - in 3rd position or higher => toggle between 2nd word, 1-, 2- past

(defun fn-mode--compute-indents ()
  "Generate a list of indentation amounts which make sense in the
given context."
  (let ((start (point)))
    (save-excursion
      (beginning-of-line)
      (let* ((ps (syntax-ppss))
             (delim (nth 1 ps))
             (str? (nth 3 ps))
             (c (if delim (char-after delim)))
             (delim-col (if delim
                            (save-excursion
                              (goto-char delim)
                              (current-column)))))
        (cond
         (str? nil)
         ((null delim) (list 0))
         ((or (= c ?\[) (= c ?\{))
          (list (+ delim-col 1)))
         (t (fn-mode--list-indents delim delim-col start)))) )))

(defun fn-mode--list-indents (delim delim-col start)
  (let* ((bfs (lambda (len)
                (buffer-substring (+ delim 1)
                                  (min (+ delim 1 len)
                                       (buffer-size)))))
         (op-is? (lambda (str)
                   (string= str (funcall bfs (length str))))))
    (cond
     ((funcall op-is? "cond") (list (+ delim-col 2)))
     ((funcall op-is? "defmacro") (list (+ delim-col 2)))
     ((funcall op-is? "defn") (list (+ delim-col 2)))
     ((funcall op-is? "do") (list (+ delim-col 2)))
     ((funcall op-is? "fn") (list (+ delim-col 2)))
     ((funcall op-is? "letfn") (list (+ delim-col 2)))
     ((funcall op-is? "with") (list (+ delim-col 2)))
     (t (fn-mode--nonspecial-list-indents delim delim-col start)))))

(defun fn-mode--nonspecial-list-indents (delim delim-col start)
  ;; first check if we can get indentation from a previous line
  (let ((p (fn-mode--prev-line-indent delim start)))
    (if p
        (list p (+ delim-col 2))
      (save-excursion
        (goto-char delim)
        (forward-char)
        ;; attempt to scan forward one sexp
        (if (condition-case nil (progn (forward-sexp) nil)
              (scan-error t))
            ;; operator
            (list (+ delim-col 1))
          (progn
            ;; skip spaces to get to the first argument
            (skip-chars-forward "[:space:]")
            (if (or (eql (char-after) ?\n) (>= (point) start))
                ;; this is the first argument
                (list (+ delim-col 1) (+ delim-col 2))
              (delete-dups
               (list (current-column) (+ delim-col 1) (+ delim-col 2))))))))))

(defun fn-mode--prev-line-indent (delim start)
  (save-excursion
    (beginning-of-line)
    (condition-case nil
        (backward-sexp)
      (scan-error (return nil)))
    (when (> (line-beginning-position) delim)
      (back-to-indentation)
      (current-column))))

(defvar fn-mode-syntax-table
  (let ((s (make-syntax-table)))
    ;; this takes care of non-printable characters
    (cl-loop for ch from 0 to 31
          do (modify-syntax-entry ch "@" s))
    ;; set all printable characters to symbol constituents
    (cl-loop for ch from 32 to 126
          do (modify-syntax-entry ch "_" s))
    ;; set all letters and numbers to word constituents
    (cl-loop for ch from 48 to 57
          do (modify-syntax-entry ch "w" s))
    (cl-loop for ch from 65 to 90
          do (modify-syntax-entry ch "w" s))
    (cl-loop for ch from 97 to 122
     do (modify-syntax-entry ch "w" s))
    ;; whitespace
    (modify-syntax-entry ?\t " " s)     ; tab
    (modify-syntax-entry ?\n ">" s)     ; newline
    (modify-syntax-entry ?\v " " s)     ; vtab
    (modify-syntax-entry ?\f " " s)     ; form feed
    (modify-syntax-entry ?\r " " s)     ; carriage return
    (modify-syntax-entry ?\  " " s)
    ;; set syntax characters
    (modify-syntax-entry ?\" "\"" s)
    (modify-syntax-entry ?\' "." s)
    (modify-syntax-entry ?\( "()" s)
    (modify-syntax-entry ?\) ")(" s)
    (modify-syntax-entry ?\, "." s)
    (modify-syntax-entry ?\; "<" s)
    (modify-syntax-entry ?\[ "(]" s)
    (modify-syntax-entry ?\\ "/" s)
    (modify-syntax-entry ?\] ")[" s)
    (modify-syntax-entry ?\` "." s)
    (modify-syntax-entry ?\{ "(}" s)
    (modify-syntax-entry ?\} "){" s)
    s))

(let ((fn-symbol-regexp "\\([[:word:]?*&^%$#@!~:_=+<>\\.-]\\|\\\\.\\)"))
  (defvar fn-mode-font-lock-defaults
    `(;; quoted symbols
      (,(concat "'\\("
                fn-symbol-regexp
                "\\)+\\_>")
       0 font-lock-constant-face)
      ;; type names
      (,(concat "\\_<[[:upper:]]" fn-symbol-regexp "+\\_>")
       0 font-lock-type-face)
      ;; keywords, &, and global symbols
      (,(concat "\\_<:" fn-symbol-regexp "+\\_>")
       0 font-lock-builtin-face)
      ("\\_<&\\_>" . font-lock-builtin-face)
      (,(concat "\\_<#/" fn-symbol-regexp "+\\_>")
       0 font-lock-builtin-face)
      ;; function names
      (,(concat "([[:space:]]*\\_<\\(defn\\|defmacro\\|letfn\\)\\_>[[:space:]]+"
                "\\(\\_<" fn-symbol-regexp "+\\_>\\)")
       (2 font-lock-function-name-face))
      ;; special operators
      (,(concat "([[:space:]]*\\_<\\(and\\|cond\\|def\\|defmacro\\|defn\\|do\\|"
                "dot\\|dollar-fn\\|if\\|import\\|fn\\|let\\|"
                "letfn\\|or\\|namespace\\|quasiquote\\|quote\\|unquote\\|"
                "unquote-splicing\\|set!\\|with\\)\\_>")
       1 font-lock-keyword-face)
      ("\\_<\\(true\\|false\\|nil\\)\\_>" 1 font-lock-constant-face)
      ("\\_<\\(self\\)\\(\\.\\|\\_>\\)" 1 font-lock-constant-face)
      ;; dollar syntax
      ("\\([$]\\)[([{`]" 1 font-lock-keyword-face)
      )))


(define-derived-mode fn-mode prog-mode "Fn"
  "Major mode for editing Fn code.

\\{fn-mode-map}"
  :abbrev-table nil
  (setq-local comment-start ";")
  (setq-local comment-padding 1)
  (setq-local comment-end "")
  (setq-local indent-line-function 'fn-mode-indent-line)
  (setq-local font-lock-defaults (list fn-mode-font-lock-defaults)))


