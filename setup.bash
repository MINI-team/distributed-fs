protoc-c --c_out=./c-client dfs.proto
protoc-c --c_out=./mock-replica dfs.proto
protoc --python_out=. dfs.proto