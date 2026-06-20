# #!/usr/bin/python3
# print("Content-Type: text/html\r\n\r\n", end="")
# try:
#     # This is a strict relative path. It only works if the current working
#     # directory of the running process is 'www/html/cgi-bin/'
#     with open("test_relative.text", "r") as f:
#         print(f"<h1>{f.read()}</h1>")
# except FileNotFoundError:
#     print("<h1>Error: Script executed in the wrong directory! File not found.</h1>")


#!/usr/bin/python3
print("Content-Type: text/html\r\n\r\n", end="")
try:
    # If chdir() isn't called, this open command looks in /goinfre/fbenalla/for_push/
    # instead of /goinfre/fbenalla/for_push/www/html/cgi-bin/
    with open("test_relative.txt", "r") as f:
        print(f"<h1>{f.read()}</h1>")
except FileNotFoundError:
    print("<h1>Error: Script executed in the wrong directory! File not found.</h1>")