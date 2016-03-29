((c++-mode (eval add-hook 'before-save-hook #'clang-format-buffer nil t))
 (nil . ((compile-command . "cmake --build build")))
)
