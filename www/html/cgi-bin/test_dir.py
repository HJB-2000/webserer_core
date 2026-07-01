# #!/usr/bin/python3



#!/usr/bin/python3
print("Content-Type: text/html\r\n\r\n", end="")
try:

    with open("test_relative.txt", "r") as f:
        print(f"<h1>{f.read()}</h1>")
except FileNotFoundError:
    print("<h1>Error: Script executed in the wrong directory! File not found.</h1>")