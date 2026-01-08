cmake -B build -S . && \
cmake --build build --target hikup-server -j $(nproc)
