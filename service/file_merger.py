import sys
import os

NUM_OF_MB = 32
BYTES_IN_MB = 1048576

fileM = open("out_"+sys.argv[1], "wb")
file_size_bytes = os.stat(sys.argv[1]).st_size
print("size (bytes): "+ str(file_size_bytes))
chunks = round(file_size_bytes/(BYTES_IN_MB*NUM_OF_MB))
chunk = 0
bytes_left = file_size_bytes - NUM_OF_MB * chunks * BYTES_IN_MB
print("bytes left: " + str(bytes_left))

while chunk < chunks:
    fileTemp = open(str(chunk) + ".chunk", "rb")
    byte = fileTemp.read(BYTES_IN_MB*NUM_OF_MB)
    fileM.write(byte)
    chunk += 1

if bytes_left > 0:
    fileTemp = open(str(chunk) + ".chunk", "rb")
    byte = fileTemp.read(bytes_left)
    fileM.write(byte)

fileM.close()