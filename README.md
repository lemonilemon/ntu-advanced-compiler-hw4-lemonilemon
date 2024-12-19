# ntu-advanced-compiler-hw4-lemonilemon

## Build

```bash
cd ntu-advanced-compiler-hw4-lemonilemon
mkdir build
cd build
cmake ..
make
cd ..
```

## Run

```bash
clang -fpass-plugin=`echo build/src/SkeletonPass.*` something.c
```

