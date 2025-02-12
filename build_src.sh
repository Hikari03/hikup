cmake -B build -S . && \
cmake --build build --target hikup -j $(nproc)
