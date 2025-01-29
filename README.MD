
# files for writing need to placed in the build/client, not build/client/data_client !
   ```bash

##############################
# LOCAL DEMO
##############################
sudo apt install protobuf-c-compiler libprotobuf-c-dev libglib2.0-dev
bash setup.bash
mkdir -p build/replica/chunks/8080 build/replica/chunks/8081 build/replica/chunks/8082
# or use make-dirs.sh (it will create directories for 10 replicas)
cd build
cmake ..
make
cp ../data_client/alphabet client/alphabet # or place other (probably larger) file inside build/client

# on separate terminals:
cd replica && ./replica 8080
cd replica && ./replica 8081
cd replica && ./replica 8082
cd server && ./server

cd client
./client write alphabet # or write-committed
./client read alphabet

# to clear replica storage use remove-chunks.sh; if you don't have folders for all replicas with ports 8080..8089 it will show some errors, but it's not a problem; or you can manually remove the files from build/replica/chunks/{port}
