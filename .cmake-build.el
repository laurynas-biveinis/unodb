((cmake-build-cmake-profiles
  (release "-DCMAKE_BUILD_TYPE=Release")
  (debug "-DCMAKE_BUILD_TYPE=Debug")
  (debug-asan "-DCMAKE_BUILD_TYPE=Debug -DSANITIZE=ON")
  (debug-tsan "-DCMAKE_BUILD_TYPE=Debug -DSANITIZE_THREAD=ON")

  (gcc7-release "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-7 -DCMAKE_CXX_COMPILER=g++-7")
  (gcc7-debug "-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-7 -DCMAKE_CXX_COMPILER=g++-7")

  (gcc8-release "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-8 -DCMAKE_CXX_COMPILER=g++-8")
  (gcc8-debug "-DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-8 -DCMAKE_CXX_COMPILER=g++-8"))

 (cmake-build-run-configs
  (test
   (:build "")
   (:run "" "make" "test"))))
