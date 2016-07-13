;;; package --- A major mode for the Sparkling scripting language
;;; Commentary:
;;; none

;; define major mode hook
;;; Code:
(defvar sparkling-mode-hook nil)

(defun sparkling-eval-replace-expr (start end)
  "Evaluate the contents of the selected region (or the region between START and END) as an expression and replace it with the result."
  (interactive "r")
  (call-process "spn" nil t nil "-e" (delete-and-extract-region start end))
)

(defun sparkling-eval-replace-stmt (start end)
  "Evaluate the contents of the selected region (or the region between START and END) as statements and replace it with the result."
  (interactive "r")
  (call-process "spn" nil t nil "-t" "-r" (delete-and-extract-region start end))
)

(defvar sparkling-eval-buffer-name "*sparkling-eval*")

(defun sparkling-eval-expr (start end)
  "Evaluate the contents of the selected region (or the region between START and END) as an expression."
  (interactive "r")
  (call-process "spn" nil sparkling-eval-buffer-name nil "-e" (buffer-substring start end))
  (deactivate-mark)
  (switch-to-buffer-other-window sparkling-eval-buffer-name)
)

(defun sparkling-eval-stmt (start end)
  "Evaluate the contents of the selected region (or the region between START and END) as statements."
  (interactive "r")
  (call-process "spn" nil sparkling-eval-buffer-name nil "-t" "-r" (buffer-substring start end))
  (deactivate-mark)
  (switch-to-buffer-other-window sparkling-eval-buffer-name)
)

(defun sparkling-close-curly ()
  (interactive)
  (insert "}")
  (sparkling-indent-line)
)

;; Define keymap
(defvar sparkling-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map "RET" 'newline-and-indent)
    (define-key map "}" 'sparkling-close-curly)
    (define-key map (kbd "C-c C-e") 'sparkling-eval-replace-expr)
    (define-key map (kbd "C-c C-r") 'sparkling-eval-replace-stmt)
    (define-key map (kbd "C-M-e") 'sparkling-eval-expr)
    (define-key map (kbd "C-M-r") 'sparkling-eval-stmt)
    map
  )
  "Keymap for Sparkling major mode"
)

;; automatically turn it on
(add-to-list 'auto-mode-alist '("\\.spn\\'" . sparkling-mode))

(defvar sparkling-font-lock-keywords
  (list
   '("\\<\\(and\\|break\\|continue\\|do\\|e\\(lse\\|xtern\\)\\|f\\(n\\|or\\)\\|if\\|let\\|not\\|or\\|return\\|sizeof\\|while\\)\\>" . font-lock-keyword-face)
   '("\\('\\w*'\\)" . font-lock-variable-name-face)
   '("\\(\\\"\\(\\\\\"\\|[^\\\"]\\)*\\\"\\)" . font-lock-string-face)
	 '("\\(nil\\|true\\|false\\)" . font-lock-constant-face)
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
      (setq indent-col (- indent-col sparkling-indent-offset)))
	  (indent-line-to indent-col))
    (if (string-match "^[ \t]+$" (thing-at-point 'line))
      (end-of-line))
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
