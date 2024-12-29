# Python script to create a 1 GB text file

# Define the file path where the text file will be saved
file_path = "large_file.txt"

# Define a sample line of text
content = "This is a line of text.\n" * 500  # Each line is approximately 500 characters

# Calculate how many times to repeat the content to create a 1GB file
repeat_count = (1024 * 1024 * 1024) // len(content)

# Write the content to the file
with open(file_path, "w") as file:
    file.write(content * repeat_count)

print(f"File created successfully at {file_path}")
