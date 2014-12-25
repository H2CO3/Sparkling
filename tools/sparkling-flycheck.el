;;; sparkling-flycheck --- flycheck module for Sparkling

(flycheck-define-checker sparkling-lint
  "Sparkling syntax checker"
  :command ("spnlint" source)
  :modes (sparkling-mode)
  :error-patterns (
	(error line-start "near line " line ", char " column ": " (message) line-end)
  )
)
(add-hook 'sparkling-mode-hook (lambda ()
				 (flycheck-select-checker 'sparkling-lint)
				 (flycheck-mode)))
(provide 'sparkling-flycheck)
