((cmake-build-cmake-profiles
  (release "-DCMAKE_BUILD_TYPE=Release")
  (release-asan "-DCMAKE_BUILD_TYPE=Release -DSANITIZE_ADDRESS=ON")
  (release-tsan "-DCMAKE_BUILD_TYPE=Release -DSANTIZE_THREAD=ON")

  (debug "-DCMAKE_BUILD_TYPE=Debug")
  (debug-asan "-DCMAKE_BUILD_TYPE=Debug -DSANITIZE_ADDRESS=ON")
  (debug-tsan "-DCMAKE_BUILD_TYPE=Debug -DSANITIZE_THREAD=ON")
  (debug-ubsan "-DCMAKE_BUILD_TYPE=Debug -DSANITIZE_UB=ON")

  (gcc-10-release "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10")
  (gcc-10-release-asan "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10 -DSANITIZE_ADDRESS=ON")
  (gcc-10-release-ubsan "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10 -DSANITIZE_UB=ON")
  (gcc-10-debug "-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10")
  (gcc-10-debug-asan "-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10 -DSANITIZE_ADDRESS=ON")
  (gcc-10-debug-tsan "-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10 -DSANITIZE_THREAD=ON")

  (gcc-release "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11")
  (gcc-release-asan "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11 -DSANITIZE_ADDRESS=ON")
  (gcc-release-ubsan "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11 -DSANITIZE_UB=ON")
  (gcc-debug "-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11")
  (gcc-debug-asan "-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11 -DSANITIZE_ADDRESS=ON")
  (gcc-debug-tsan "-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11 -DSANITIZE_THREAD=ON")

  (llvm-release "-DCMAKE_PREFIX_PATH=/usr/local/opt/llvm -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/local/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/usr/local/opt/llvm/bin/clang++")
  (llvm-release-tsan "-DCMAKE_PREFIX_PATH=/usr/local/opt/llvm -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/local/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/usr/local/opt/llvm/bin/clang++ -DSANITIZE_THREAD=ON")
  (llvm-debug "-DCMAKE_PREFIX_PATH=/usr/local/opt/llvm -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=/usr/local/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/usr/local/opt/llvm/bin/clang++")
  (llvm-debug-asan "-DCMAKE_PREFIX_PATH=/usr/local/opt/llvm -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=/usr/local/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/usr/local/opt/llvm/bin/clang++ -DSANITIZE_ADDRESS=ON")
  (llvm-debug-tsan "-DCMAKE_PREFIX_PATH=/usr/local/opt/llvm -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=/usr/local/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/usr/local/opt/llvm/bin/clang++ -DSANITIZE_THREAD=ON")

  (gcc-static-analysis-debug "-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11 -DSTATIC_ANALYSIS=ON")
  (gcc-static-analysis-release "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11 -DSTATIC_ANALYSIS=ON"))

 (cmake-build-run-configs
  (test
   (:build "")
   (:run "" "make" "test"))))
