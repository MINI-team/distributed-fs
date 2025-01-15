protoc-c --c_out=./shared dfs.proto

# find . -name "0.chunk" -exec rm {} +
# find . -name "1.chunk" -exec rm {} +
find . -name "*.chunk" -exec rm {} +

rm ./build/client/output.txt