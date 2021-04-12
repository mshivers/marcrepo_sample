greyhound
=========

Initial cut for valuation based treasury market making and removal models.


Installation
------------
Google test, google mock, and boost are required. For a few reasons, I did not include submodules in this repo.

### TL;DR:

```bash
mkdir thirdparty && cd thirdparty && git clone https://github.com/google/googletest 
pushd googletest/googletest && mkdir build && pushd build && cmake .. && make -j8 && mv *.a ../ && popd && popd;
pushd googletest/googlemock && mkdir build && pushd build && cmake .. && make -j8 && mv *.a ../../googletest && popd && popd;
```

For building to work there must be a thirdparty/googletest/ folder at the root level, which in turn contains googletest and googlemock folders. All the library files should be in thirdparty/googletest/googletest. 

1. Google test/Google mock: https://github.com/google/googletest
2. Boost: https://github.com/boostorg/boost (or your favorite package manager)

