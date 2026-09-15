cmake -B build -S . -DHIKUP_STATIC=ON && \
cmake --build build --target hikup-server -j $(nproc)
