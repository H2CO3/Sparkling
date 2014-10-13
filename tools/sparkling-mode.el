;;; package --- A major mode for the Sparkling scripting language
;;; Commentary:
;;; none

;; define major mode hook
;;; Code:
(defvar sparkling-mode-hook nil)

;; auto-insert curly brace pair
;; shamelessly stolen from http://stackoverflow.com/q/3801147
(defun sparkling-mode-insert-curly ()
  (interactive)
  (insert "{")
  (let ((pps (syntax-ppss)))
    (when (and (eolp) (not (or (nth 3 pps) (nth 4 pps)))) ;; EOL and not in string or comment
      ;; (sparkling-indent-line) ;; needed only if return doesn't auto-indent
      (insert "\n\n}")
      (sparkling-indent-line)
      (forward-line -1)
      (sparkling-indent-line)
      (forward-line -1)
      (sparkling-indent-line)
      (forward-line 1)
      (sparkling-indent-line))
  )
)

;; Define keymap
(defvar sparkling-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map "RET" 'newline-and-indent)
    (define-key map "{" 'sparkling-mode-insert-curly)
    map
  )
  "Keymap for Sparkling major mode"
)

;; automatically turn it on
(add-to-list 'auto-mode-alist '("\\.spn\\'" . sparkling-mode))

(defvar sparkling-font-lock-keywords
  (list
   '("\\<\\(a\\(nd\\|rgv\\)\\|break\\|con\\(st\\|tinue\\)\\|do\\|else\\|f\\(or\\|unction\\)\\|global\\|if\\|let\\|not\\|or\\|return\\|sizeof\\|typeof\\|var\\|while\\)\\>" . font-lock-keyword-face)
   '("\\('\\w*'\\)" . font-lock-variable-name-face)
   '("\\(\\\"\\(\\\\\"\\|[^\\\"]\\)*\\\"\\)" . font-lock-string-face)
	 '("\\(nil\\|null\\|true\\|false\\)" . font-lock-constant-face)
   )
  "Syntax highlighting for Sparkling mode"
)

(set-face-foreground 'font-lock-string-face "Gold")

;; Indentation

(defun sparkling-indent-line ()
  "Indent current line for `sparkling-mode'."
  (interactive)
  (let ((indent-col 0))
    (save-excursion
      (beginning-of-line)
      (condition-case nil
	  (while t
	    (backward-up-list 1)
	    (when (looking-at "[{]")
	      (setq indent-col (+ indent-col sparkling-indent-offset))))
	(error nil)))
    (save-excursion
      (back-to-indentation)
      (when (and (looking-at "[}]") (>= indent-col sparkling-indent-offset))
	(setq indent-col (- indent-col sparkling-indent-offset))))
    (indent-line-to indent-col)
  )
)

;; Syntax table
(defvar sparkling-mode-syntax-table
  (let ((sparkling-mode-syntax-table (make-syntax-table)))
	;; This is added so entity names with underscores can be more easily parsed
    (modify-syntax-entry ?_ "w" sparkling-mode-syntax-table)

	;; C-style comments
    (modify-syntax-entry ?/ ". 124b" sparkling-mode-syntax-table)
    (modify-syntax-entry ?# "< b" sparkling-mode-syntax-table)
    (modify-syntax-entry ?* ". 23" sparkling-mode-syntax-table)
    (modify-syntax-entry ?\n "> b" sparkling-mode-syntax-table)
    sparkling-mode-syntax-table)
  "Syntax table for sparkling-mode"
)

;; for auto-commenting, auto-uncommenting
(setq comment-start "//")

(define-derived-mode sparkling-mode fundamental-mode "Sparkling"
  "Major mode for Sparkling source"
  (make-local-variable 'sparkling-indent-offset)
  (set (make-local-variable 'font-lock-defaults) '(sparkling-font-lock-keywords))
  (set (make-local-variable 'indent-line-function) 'sparkling-indent-line)
)

(defvar sparkling-indent-offset 4
  "*Indentation offset for `sparkling-mode'.")

(set (make-local-variable 'font-lock-defaults) '(sparkling-font-lock-keywords))

(set (make-local-variable 'indent-line-function) 'sparkling-indent-line)

(setq major-mode 'sparkling-mode)
(setq mode-name "Sparkling")
(run-hooks 'sparkling-mode-hook)

(provide 'sparkling-mode)
;;; sparkling-mode.el ends here
