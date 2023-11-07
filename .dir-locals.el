;; StackOverflow'ed from https://stackoverflow.com/a/11474657/80458
;; An alternative would be to use `projectile-project-root', but the current
;; method works without Projectile too.
((nil . ((eval . (setq-local ispell-personal-dictionary
                             (expand-file-name
                              ".ispell.dict"
                              (file-name-directory
                               (let ((d (dir-locals-find-file "./")))
                                 (if (stringp d) d (car d)))))))))
 (c++-mode . ((c-tab-always-indent t)
              (c-file-style . "Google"))))
