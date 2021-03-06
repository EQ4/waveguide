#!/bin/zsh
setopt extended_glob
ls lib/*.(h|cpp) | xargs clang-format -i
ls common/*.(h|cpp) | xargs clang-format -i
ls cmd/*.(h|cpp) | xargs clang-format -i
ls tests/*.(h|cpp) | xargs clang-format -i
ls rayverb/*.(h|cpp)~rayverb/hrtf.cpp | xargs clang-format -i
