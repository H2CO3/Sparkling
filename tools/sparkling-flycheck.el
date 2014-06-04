;;; sparkling-flycheck --- flycheck module for Sparkling

(flycheck-define-checker sparkling-lint
  "Sparkling syntax checker"
  :command ("spnlint" source)
  :modes (sparkling-mode)
  :error-patterns ((error line-start "syntax error near line " line ": " (message) line-end)
		   (error line-start "semantic error near line " line ": " (message) line-end)
  )
)
(add-hook 'sparkling-mode-hook (lambda ()
				 (flycheck-select-checker 'sparkling-lint)
				 (flycheck-mode)))
(provide 'sparkling-flycheck)
