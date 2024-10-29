import sys
import os

NUM_OF_MB = 32
BYTES_IN_MB = 1048576

file_in = open(sys.argv[1], "rb")
file_size_bytes = os.stat(sys.argv[1]).st_size

print("size (bytes): "+ str(file_size_bytes))
chunks = round(file_size_bytes/(BYTES_IN_MB*NUM_OF_MB))
print("chunks: " + str(chunks))

bytes_left = file_size_bytes - NUM_OF_MB * chunks * BYTES_IN_MB
print("bytes left: " + str(bytes_left))

chunk = 0

while chunk < chunks:
    bytes = file_in.read(NUM_OF_MB*BYTES_IN_MB)
    file_out = open(str(chunk) + ".chunk", "wb")
    file_out.write(bytes)
    file_out.close()
    chunk += 1

if bytes_left > 0:
    bytes = file_in.read(bytes_left)
    file_out = open(str(chunk) + ".chunk", "wb")
    file_out.write(bytes)
    file_out.close()

file_in.close()