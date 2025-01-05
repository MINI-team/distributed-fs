import sys
import hashlib

def md5sum(filename):
    with open(filename, 'rb') as f:
        file_data = f.read()
    return hashlib.md5(file_data).hexdigest()

def compare_md5(file1, file2):
    hash1 = md5sum(file1)
    hash2 = md5sum(file2)

    print(f"MD5 for {file1}: {hash1}")
    print(f"MD5 for {file2}: {hash2}")

    if hash1 == hash2:
        print("Files have identical MD5 checksums.")
    else:
        print("Files have different MD5 checksums.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <file1> <file2>")
        sys.exit(1)
    compare_md5(sys.argv[1], sys.argv[2])
